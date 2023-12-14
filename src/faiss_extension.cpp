#include "duckdb/common/preserved_error.hpp"
#include "duckdb/common/shared_ptr.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/types/selection_vector.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/execution/column_binding_resolver.hpp"
#include "duckdb/execution/execution_context.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/prepared_statement.hpp"
#include "duckdb/parallel/interrupt.hpp"
#include "duckdb/planner/expression.hpp"
#include "duckdb/planner/logical_operator.hpp"
#include "faiss/Index.h"
#include "faiss/MetricType.h"
#include "faiss/impl/IDSelector.h"

#include <cstddef>
#include <duckdb/common/optional_ptr.hpp>
#include <duckdb/common/types/data_chunk.hpp>
#include <duckdb/execution/expression_executor.hpp>
#include <duckdb/main/database.hpp>
#include <duckdb/parallel/thread_context.hpp>
#include <duckdb/parser/query_node/select_node.hpp>
#include <iostream>
#include <ostream>
#include <string>
#include <vector>
#define DUCKDB_EXTENSION_MAIN

#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/parser/parser.hpp"
#include "duckdb/planner/binder.hpp"
#include "duckdb/storage/object_cache.hpp"
#include "faiss_extension.hpp"

#include <cstdint>
#include <faiss/impl/FaissException.h>
#include <faiss/index_factory.h>
#include <faiss/index_io.h>
#include <random>
#include <shared_mutex>

namespace duckdb {

struct IndexEntry : ObjectCacheEntry {
	unique_ptr<faiss::Index> index;

	// This is true if the index needs training. In the future,
	// this can also be false if manual training enabled.
	bool needs_training = true;

	int dimension = 0; // This can easily be obtained from the index, doing only a pointer dereference.
	vector<unique_ptr<float[]>> index_data; // Currently I do not see a use for this
	unique_ptr<std::mutex>
	    faiss_lock; // c++11 doesnt have a shared_mutex, introduced in c++14. duckdb is build with c++11

	vector<size_t> size;

	unique_ptr<std::mutex> add_lock;
	// Store data for the add function (which can be done in parallel) and add all at once
	vector<unique_ptr<float[]>> add_data;
	vector<unique_ptr<faiss::idx_t[]>> add_labels;

	static string ObjectType() {
		return "faiss_index";
	}

	string GetObjectType() override {
		return IndexEntry::ObjectType();
	}
};

struct CreateFunctionData : public TableFunctionData {
	string key;
	int dimension = 0;
	string description;
};

static unique_ptr<FunctionData> CreateBind(ClientContext &, TableFunctionBindInput &input,
                                           vector<LogicalType> &return_types, vector<string> &names) {
	auto result = make_uniq<CreateFunctionData>();
	return_types.emplace_back(LogicalType::BOOLEAN);
	names.emplace_back("Success");

	result->key = input.inputs[0].ToString();
	result->dimension = input.inputs[1].GetValue<int>();
	result->description = input.inputs[2].ToString();

	return std::move(result);
}

static void CreateFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &) {
	auto &bind_data = data_p.bind_data->Cast<CreateFunctionData>();
	auto &object_cache = ObjectCache::GetObjectCache(context);

	if (object_cache.Get<IndexEntry>(bind_data.key)) {
		throw InvalidInputException("Index %s already exists.", bind_data.key);
	}

	auto entry = make_shared<IndexEntry>();
	entry->dimension = bind_data.dimension;
	entry->index = unique_ptr<faiss::Index>(faiss::index_factory(entry->dimension, bind_data.description.c_str()));
	entry->needs_training = !entry->index.get()->is_trained;
	entry->faiss_lock = unique_ptr<std::mutex>(new std::mutex());
	entry->add_lock = unique_ptr<std::mutex>(new std::mutex());

	object_cache.Put(bind_data.key, std::move(entry));
}

struct DestroyFunctionData : public TableFunctionData {
	string key;
};

static unique_ptr<FunctionData> DestroyBind(ClientContext &, TableFunctionBindInput &input,
                                            vector<LogicalType> &return_types, vector<string> &names) {
	auto result = make_uniq<DestroyFunctionData>();
	return_types.emplace_back(LogicalType::BOOLEAN);
	names.emplace_back("Success");

	result->key = input.inputs[0].ToString();
	return std::move(result);
}

static void DestroyFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &) {
	auto &bind_data = data_p.bind_data->Cast<DestroyFunctionData>();
	auto &object_cache = ObjectCache::GetObjectCache(context);

	if (!object_cache.Get<IndexEntry>(bind_data.key)) {
		throw InvalidInputException("Could not find index %s.", bind_data.key);
	}

	object_cache.Put(bind_data.key, nullptr);
}

static unique_ptr<Vector> ListVectorToFaiss(ClientContext &context, Vector &input_vector, idx_t n_lists,
                                            idx_t dimension) {
	if (input_vector.GetType().id() != LogicalTypeId::LIST) {
		throw InvalidInputException("Need list type for embeddings vectors");
	}

	input_vector.Flatten(n_lists); // FIXME use canonical
	D_ASSERT(input_vector.GetVectorType() == VectorType::FLAT_VECTOR);

	auto list_entries = ListVector::GetData(input_vector);

	for (idx_t row_idx = 0; row_idx < n_lists; row_idx++) {
		if (list_entries[row_idx].length != dimension) {
			throw InvalidInputException("All list vectors need to have length %d, got %llu at index %llu", dimension,
			                            list_entries[row_idx].length, row_idx);
		}
	}

	auto list_child = ListVector::GetEntry(input_vector);
	// TODO use canonical here as well

	auto data_elements = n_lists * dimension;

	list_child.Flatten(data_elements);

	auto cast_result = make_uniq<Vector>(LogicalType::FLOAT, data_elements);
	VectorOperations::Cast(context, list_child, *cast_result, data_elements);
	return cast_result;
}

struct AddData : TableFunctionData {
	string key;
	bool has_labels = false;
};

static unique_ptr<FunctionData> AddBind(ClientContext &, TableFunctionBindInput &input,
                                        vector<LogicalType> &return_types, vector<string> &names) {
	auto bind_data = make_uniq<AddData>();

	if (input.input_table_names.size() == 2) { // we have labels
		bind_data->has_labels = true;
	}

	return_types.emplace_back(LogicalType::BOOLEAN);
	names.emplace_back("Success");

	bind_data->key = input.inputs[1].ToString();

	return bind_data;
}

static unique_ptr<GlobalTableFunctionState> AddGlobalInit(ClientContext &context, TableFunctionInitInput &input) {
	return make_uniq<GlobalTableFunctionState>();
}

static OperatorResultType AddFunction(ExecutionContext &context, TableFunctionInput &data_p, DataChunk &input,
                                      DataChunk &) {
	auto bind_data = data_p.bind_data->Cast<AddData>();
	auto &object_cache = ObjectCache::GetObjectCache(context.client);
	auto entry_ptr = object_cache.Get<IndexEntry>(bind_data.key);
	if (!entry_ptr) {
		throw InvalidInputException("Could not find index %s.", bind_data.key);
	}
	auto &entry = *entry_ptr;
	auto data_elements = input.size() * entry.dimension;

	auto child_vec = ListVectorToFaiss(context.client, bind_data.has_labels ? input.data[1] : input.data[0],
	                                   input.size(), entry.dimension);
	auto child_ptr = FlatVector::GetData<float>(*child_vec);

	faiss::idx_t *label_ptr;
	if (bind_data.has_labels) {
		Vector label_cast_vec(LogicalType::BIGINT);
		VectorOperations::Cast(context.client, input.data[0], label_cast_vec, input.size());
		label_ptr = FlatVector::GetData<faiss::idx_t>(label_cast_vec);
	}

	// If we do not need training, no need to use do it all at the end!
	if (!entry.needs_training) {
		entry.faiss_lock.get()->lock();
		if (bind_data.has_labels) {
			entry.index->add_with_ids((faiss::idx_t)input.size(), child_ptr, label_ptr);
		} else {
			entry.index->add((faiss::idx_t)input.size(), child_ptr);
		}
		entry.faiss_lock.get()->unlock();
		return OperatorResultType::NEED_MORE_INPUT;
	}

	auto index_data = unique_ptr<float[]>(new float[data_elements]);
	unique_ptr<faiss::idx_t[]> label_data;
	memcpy(index_data.get(), child_ptr, data_elements * sizeof(float));

	if (bind_data.has_labels) {
		label_data = unique_ptr<faiss::idx_t[]>(new faiss::idx_t[data_elements]);
		memcpy(label_data.get(), label_ptr, input.size() * sizeof(long));
	}

	entry.add_lock.get()->lock();
	entry.add_data.push_back(std::move(index_data));
	if (bind_data.has_labels) {
		entry.add_labels.push_back(std::move(label_data));
	}
	entry.size.push_back(std::move(input.size()));
	entry.add_lock.get()->unlock();

	return OperatorResultType::NEED_MORE_INPUT;
}

static OperatorFinalizeResultType AddFinaliseFunction(ExecutionContext &context, TableFunctionInput &data_p,
                                                      DataChunk &) {
	auto bind_data = data_p.bind_data->Cast<AddData>();
	auto &object_cache = ObjectCache::GetObjectCache(context.client);
	auto entry_ptr = object_cache.Get<IndexEntry>(bind_data.key);
	if (!entry_ptr) {
		throw InvalidInputException("Could not find index %s.", bind_data.key);
	}

	auto &entry = *entry_ptr;
	entry.add_lock.get()->lock();
	size_t total_elements = 0;
	for (size_t size : entry.size) {
		total_elements += size;
	}

	auto vector_data = unique_ptr<float[]>(new float[total_elements * entry.dimension]);
	auto label_data = unique_ptr<faiss::idx_t[]>(new faiss::idx_t[total_elements]);
	size_t offset = 0;
	for (size_t i = 0; i < entry.size.size(); i++) {
		size_t size = entry.size[i];

		// Pointer aritmatic, fun!
		memcpy(vector_data.get() + offset, entry.add_data[i].get(), size * entry.dimension * sizeof(float));
		if (entry.add_data.size() == entry.add_labels.size()) {
			memcpy(label_data.get() + offset, entry.add_labels[i].get(), size * sizeof(faiss::idx_t));
		}
		offset += size;
	}
	entry.add_lock.get()->unlock();

	if (entry.add_data.size() == 0) {
		return OperatorFinalizeResultType::FINISHED;
	}

	entry.faiss_lock.get()->lock();
	try {
		entry.index->train((faiss::idx_t)total_elements, vector_data.get());
	} catch (faiss::FaissException exception) {
		std::string msg = exception.msg;
		if (msg.find("should be at least as large as number of clusters") != std::string::npos) {
			throw InvalidInputException(
			    "Index needs to be trained, but amount of datapoints is too small. Considere adding more data. (" +
			        msg + ")",
			    bind_data.key);
		}
	}

	if (entry.add_data.size() == entry.add_labels.size()) {
		entry.index->add_with_ids((faiss::idx_t)total_elements, vector_data.get(), label_data.get());
	} else {
		entry.index->add((faiss::idx_t)total_elements, vector_data.get());
	}
	entry.faiss_lock.get()->unlock();

	return OperatorFinalizeResultType::FINISHED;
}

// Searches the faiss index contained in the IndexEntry using the given queries and inputdata and search params. The
// results are stored in the outputvector as a struct-type of the form {rank, id, distance}.
void searchIntoVector(ClientContext &ctx, IndexEntry &entry, Vector inputdata, size_t nQueries, size_t nResults,
                      faiss::SearchParameters *searchParams, Vector &output) {
	unique_ptr<Vector> child_vec = ListVectorToFaiss(ctx, inputdata, nQueries, entry.dimension);
	auto child_ptr = FlatVector::GetData<float>(*child_vec);

	unique_ptr<faiss::idx_t[]> labels = unique_ptr<faiss::idx_t[]>(new faiss::idx_t[nQueries * nResults]);
	unique_ptr<float[]> distances = unique_ptr<float[]>(new float[nQueries * nResults]);

	entry.faiss_lock.get()->lock(); //  this should be a readlock once c++17 is supported
	entry.index->search((faiss::idx_t)nQueries, child_ptr, nResults, distances.get(), labels.get(), searchParams);
	entry.faiss_lock.get()->unlock();

	ListVector::SetListSize(output, nQueries * nResults);
	ListVector::Reserve(output, nQueries * nResults);
	list_entry_t *list_ptr = ListVector::GetData(output);
	Vector &result_struct_vector = ListVector::GetEntry(output);
	vector<unique_ptr<Vector>> &struct_entries = StructVector::GetEntries(result_struct_vector);
	int *rank_ptr = FlatVector::GetData<int32_t>(*struct_entries[0]);
	long *label_ptr = FlatVector::GetData<int64_t>(*struct_entries[1]);
	float *distance_ptr = FlatVector::GetData<float>(*struct_entries[2]);

	idx_t list_offset = 0;

	for (idx_t row_idx = 0; row_idx < nQueries; row_idx++) {
		list_ptr[row_idx].length = nResults;
		list_ptr[row_idx].offset = list_offset;

		for (idx_t res_idx = 0; res_idx < nResults; res_idx++) {
			rank_ptr[list_offset + res_idx] = (int32_t)res_idx;
			label_ptr[list_offset + res_idx] = labels[row_idx * nResults + res_idx];
			distance_ptr[list_offset + res_idx] = distances[row_idx * nResults + res_idx];
		}
		list_offset += nResults;
	}
}

// TODO: search could be a table function, which would require more copying but
// could result in allowing duckdb to "ask for more" if needed
void SearchFunction(DataChunk &input, ExpressionState &state, Vector &output) {
	string key = input.data[0].GetValue(0).ToString();

	auto &object_cache = ObjectCache::GetObjectCache(state.GetContext());
	auto entry_ptr = object_cache.Get<IndexEntry>(key);
	if (!entry_ptr) {
		throw InvalidInputException("Could not find index %s.", key);
	}
	size_t nQueries = input.size();
	size_t nResults = input.data[1].GetValue(0).GetValue<int32_t>();
	faiss::SearchParameters searchParams;

	searchIntoVector(state.GetContext(), *entry_ptr, input.data[2], nQueries, nResults, &searchParams, output);
}

// TODO: ensure that the input vectors are ordered in the query itself??
void ProcessSelectionvector(unique_ptr<DataChunk> &chunk, std::vector<uint8_t> &output) {
	Vector data = chunk->data[0];
	Vector ids = chunk->data[1];
	uint8_t *dataBytes = data.GetData();
	uint64_t *idBytes = (uint64_t *)ids.GetData();

	// TODO: check if it is sequential for optimised simd
	// TODO: use SIMD for this, as it will be very fast or (PEXT does 64 bits at a time, or 8 bools).
	for (int i = 0; i < chunk->size(); i++) {
		// in case of flatvector we can directly access this
		uint64_t id = idBytes[i];
		if (output.size() <= id) {
			output.resize(id);
		}
		int arrIndex = id / 8;
		int u8Index = id % 8;
		if (dataBytes[i]) {
			output[arrIndex] = output[arrIndex] | (1 << u8Index);
		}
	}
}

// TODO: search could be a table function, which would require more copying but
// could result in allowing duckdb to "ask for more" if needed
void SearchFunctionFilter(DataChunk &input, ExpressionState &state, Vector &output) {
	auto &object_cache = ObjectCache::GetObjectCache(state.GetContext());
	auto key = input.data[0].GetValue(0).ToString();

	auto entry_ptr = object_cache.Get<IndexEntry>(key);
	if (!entry_ptr) {
		throw InvalidInputException("Could not find index %s.", key);
	}
	auto &entry = *entry_ptr;

	auto n_results = input.data[1].GetValue(0).GetValue<int32_t>();

	// Once possible use prepared statements, currently not possible to use variables for tables (SELECT * FROM $1
	// doesnt parse) use std::format in c++20, this is really ugly
	std::stringstream ss;
	// Unlike the normal nomenclature, (u)int1 means int of 1 byte, and (u)int8 means 8 bytes.
	ss << "SELECT CAST(" << input.data[3].GetValue(0).GetValue<string>() << " AS INT1), CAST("
	   << input.data[4].GetValue(0).GetValue<string>() << " AS INT8) from "
	   << input.data[5].GetValue(0).GetValue<string>();

	string filterExpression = ss.str();

	shared_ptr<DatabaseInstance> db = state.GetContext().db;
	shared_ptr<ClientContext> subcommection = make_shared<ClientContext>(db);
	unique_ptr<QueryResult> result = subcommection->Query(filterExpression, true);

	if (result->HasError()) {
		throw InvalidInputException("uable to execute filter query: %s", result->GetError());
	}
	vector<uint8_t> mask = vector<uint8_t>();
	unique_ptr<DataChunk> chunk = unique_ptr<DataChunk>();
	PreservedError error;
	while (result->TryFetch(chunk, error) && chunk) {
		ProcessSelectionvector(chunk, mask);
	}

	// create selector
	faiss::IDSelectorBitmap selector = faiss::IDSelectorBitmap(mask.size(), mask.data());
	faiss::SearchParameters searchParams;
	searchParams.sel = &selector;

	// === normal search ===
	size_t nQueries = input.size();
	size_t nResults = input.data[1].GetValue(0).GetValue<int32_t>();

	searchIntoVector(state.GetContext(), entry, input.data[2], nQueries, nResults, &searchParams, output);
}

struct SaveFunctionData : public TableFunctionData {
	string key;
	string filename;
};

static unique_ptr<FunctionData> SaveBind(ClientContext &, TableFunctionBindInput &input,
                                         vector<LogicalType> &return_types, vector<string> &names) {
	auto result = make_uniq<SaveFunctionData>();
	return_types.emplace_back(LogicalType::BOOLEAN);
	names.emplace_back("Success");

	result->key = input.inputs[0].ToString();
	result->filename = input.inputs[1].ToString();

	return std::move(result);
}

// Duckdb has a loading and saving mechanism, but this is for tables.
// Since faiss does not use duckdb tables for data storage we cannot use this integration.
// It would be nice if there would be a mechanism to associate this with the database, and every
// save of the database would also include the index. However, this is probably not
// supported on all export formats, like parquet.
static void SaveFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &) {
	SaveFunctionData bind_data = data_p.bind_data->Cast<SaveFunctionData>();
	auto &object_cache = ObjectCache::GetObjectCache(context);

	auto entry_ptr = object_cache.Get<IndexEntry>(bind_data.key);
	if (!entry_ptr) {
		throw InvalidInputException("Could not find index %s.", bind_data.key);
	}

	auto &entry = *entry_ptr;
	faiss::Index *index = &*entry.index;
	faiss::write_index(index, bind_data.filename.c_str());
}

struct LoadFunctionData : public TableFunctionData {
	string key;
	string filename;
};

static unique_ptr<FunctionData> LoadBind(ClientContext &, TableFunctionBindInput &input,
                                         vector<LogicalType> &return_types, vector<string> &names) {
	auto result = make_uniq<LoadFunctionData>();
	return_types.emplace_back(LogicalType::BOOLEAN);
	names.emplace_back("Success");

	result->key = input.inputs[0].ToString();
	result->filename = input.inputs[1].ToString();

	return std::move(result);
}

// Duckdb has a loading and saving mechanism, but this is for tables.
// Since faiss does not use duckdb tables for data storage we cannot use this integration.
// It would be nice if there would be a mechanism to associate this with the database, and every
// save of the database would also include the index. However, this is probably not
// supported on all export formats, like parquet.
static void LoadFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &) {
	LoadFunctionData bind_data = data_p.bind_data->Cast<LoadFunctionData>();
	auto &object_cache = ObjectCache::GetObjectCache(context);

	auto entry_ptr = object_cache.Get<IndexEntry>(bind_data.key);
	if (entry_ptr) {
		throw InvalidInputException("Could not find index %s.", bind_data.key);
	}

	auto entry = make_shared<IndexEntry>();
	entry->index = unique_ptr<faiss::Index>(faiss::read_index(bind_data.filename.c_str()));
	entry->dimension = entry->index->d;

	object_cache.Put(bind_data.key, std::move(entry));
}

static void LoadInternal(DatabaseInstance &instance) {
	Connection con(instance);
	con.BeginTransaction();
	auto &catalog = Catalog::GetSystemCatalog(*con.context);

	{
		TableFunction create_func("faiss_create", {LogicalType::VARCHAR, LogicalType::INTEGER, LogicalType::VARCHAR},
		                          CreateFunction, CreateBind);
		CreateTableFunctionInfo create_info(create_func);
		catalog.CreateTableFunction(*con.context, &create_info);
	}

	{
		TableFunction add_function("faiss_add", {LogicalType::TABLE, LogicalType::VARCHAR}, nullptr, AddBind,
		                           AddGlobalInit);
		add_function.in_out_function = AddFunction;
		add_function.in_out_function_final = AddFinaliseFunction;
		CreateTableFunctionInfo add_info(add_function);
		catalog.CreateTableFunction(*con.context, &add_info);
	}

	{
		child_list_t<LogicalType> struct_children;
		struct_children.emplace_back("rank", LogicalType::INTEGER);
		struct_children.emplace_back("label", LogicalType::BIGINT);
		struct_children.emplace_back("distance", LogicalType::FLOAT);
		auto return_type = LogicalType::LIST(LogicalType::STRUCT(std::move(struct_children)));

		ScalarFunction search_function(
		    "faiss_search", {LogicalType::VARCHAR, LogicalType::INTEGER, LogicalType::LIST(LogicalType::ANY)},
		    return_type, SearchFunction);
		CreateScalarFunctionInfo search_info(search_function);
		catalog.CreateFunction(*con.context, search_info);
	}

	{
		child_list_t<LogicalType> struct_children;
		struct_children.emplace_back("rank", LogicalType::INTEGER);
		struct_children.emplace_back("label", LogicalType::BIGINT);
		struct_children.emplace_back("distance", LogicalType::FLOAT);
		auto return_type = LogicalType::LIST(LogicalType::STRUCT(std::move(struct_children)));

		ScalarFunction search_function_filter("faiss_search_filter",
		                                      {LogicalType::VARCHAR, LogicalType::INTEGER,
		                                       LogicalType::LIST(LogicalType::ANY), LogicalType::VARCHAR,
		                                       LogicalType::VARCHAR, LogicalType::VARCHAR},
		                                      return_type, SearchFunctionFilter);
		CreateScalarFunctionInfo search_info(search_function_filter);
		catalog.CreateFunction(*con.context, search_info);
	}

	{
		TableFunction create_func("faiss_destroy", {LogicalType::VARCHAR}, DestroyFunction, DestroyBind);
		CreateTableFunctionInfo create_info(create_func);
		catalog.CreateTableFunction(*con.context, &create_info);
	}

	// IO functions
	{
		TableFunction save_function("faiss_save", {LogicalType::VARCHAR, LogicalType::VARCHAR}, SaveFunction, SaveBind);
		CreateTableFunctionInfo add_info(save_function);
		catalog.CreateTableFunction(*con.context, &add_info);
	}

	{
		TableFunction load_function("faiss_load", {LogicalType::VARCHAR, LogicalType::VARCHAR}, LoadFunction, LoadBind);
		CreateTableFunctionInfo add_info(load_function);
		catalog.CreateTableFunction(*con.context, &add_info);
	}

	con.Commit();
}

void FaissExtension::Load(DuckDB &db) {
	LoadInternal(*db.instance);
}

std::string FaissExtension::Name() {
	return "faiss";
}

} // namespace duckdb

extern "C" {

DUCKDB_EXTENSION_API void faiss_init(duckdb::DatabaseInstance &db) {
	LoadInternal(db);
}

DUCKDB_EXTENSION_API const char *faiss_version() {
	return duckdb::DuckDB::LibraryVersion();
}
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif

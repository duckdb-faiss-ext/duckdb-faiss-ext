#define DUCKDB_EXTENSION_MAIN

#include "faiss_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"

#include <faiss/index_factory.h>

#include <random>

#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"

namespace duckdb {

struct IndexEntry {
	unique_ptr<faiss::Index> index;
	int dimension = 0;
	vector<unique_ptr<float[]>> index_data;
};

static unordered_map<string, IndexEntry> indexes;

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

static void CreateFunction(ClientContext &, TableFunctionInput &data_p, DataChunk &) {
	auto &bind_data = data_p.bind_data->Cast<CreateFunctionData>();

	if (indexes.find(bind_data.key) != indexes.end()) {
		throw InvalidInputException("Index %s already exists.", bind_data.key);
	}

	IndexEntry entry;
	entry.dimension = bind_data.dimension;
	entry.index = unique_ptr<faiss::Index>(faiss::index_factory(entry.dimension, bind_data.description.c_str()));

	indexes[bind_data.key] = std::move(entry);
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

static void DestroyFunction(ClientContext &, TableFunctionInput &data_p, DataChunk &) {
	auto &bind_data = data_p.bind_data->Cast<DestroyFunctionData>();
	if (indexes.find(bind_data.key) == indexes.end()) {
		throw InvalidInputException("Could not find index %s.", bind_data.key);
	}
	indexes.erase(bind_data.key);
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
	auto result = make_uniq<AddData>();

	if (input.input_table_names.size() == 2) { // we have labels
		result->has_labels = true;
	}

	return_types.emplace_back(LogicalType::BOOLEAN);
	names.emplace_back("Success");

	result->key = input.inputs[0].ToString();

	return result;
}

static OperatorResultType AddFunction(ExecutionContext &context, TableFunctionInput &data_p, DataChunk &input,
                                      DataChunk &) {
	auto bind_data = data_p.bind_data->Cast<AddData>();
	if (indexes.find(bind_data.key) == indexes.end()) {
		throw InvalidInputException("Could not find index %s.", bind_data.key);
	}

	auto &entry = indexes[bind_data.key];

	// TODO support adding with labels, first column of table ay
	auto data_elements = input.size() * entry.dimension;

	auto child_vec = ListVectorToFaiss(context.client, bind_data.has_labels ? input.data[1] : input.data[0],
	                                   input.size(), entry.dimension);
	auto child_ptr = FlatVector::GetData<float>(*child_vec);
	auto index_data = unique_ptr<float[]>(new float[data_elements]);
	memcpy(
	    index_data.get(), child_ptr,
	    data_elements *
	        sizeof(float)); // TODO we should allocate this once, keep it around and then materialize the lists into it

	if (bind_data.has_labels) {
		Vector label_cast_vec(LogicalType::BIGINT);
		VectorOperations::Cast(context.client, input.data[0], label_cast_vec, input.size());
		label_cast_vec.Flatten(input.size());
		auto label_ptr = FlatVector::GetData<int64_t>(label_cast_vec);
		indexes[bind_data.key].index->add_with_ids((faiss::idx_t)input.size(), index_data.get(), label_ptr);
	} else {
		indexes[bind_data.key].index->add((faiss::idx_t)input.size(), index_data.get());
	}
	entry.index_data.push_back(std::move(index_data));

	return OperatorResultType::NEED_MORE_INPUT;
}

void SearchFunction(DataChunk &input, ExpressionState &state, Vector &output) {
	// TODO how do we keep the index from being destroyed while we run? shared pointer in bind data?

	auto key = input.data[0].GetValue(0).ToString();
	auto n_results = input.data[1].GetValue(0).GetValue<int32_t>();

	if (indexes.find(key) == indexes.end()) {
		throw InvalidInputException("Could not find index %s.", key);
	}

	auto &entry = indexes[key];

	auto child_vec = ListVectorToFaiss(state.GetContext(), input.data[2], input.size(), entry.dimension);
	auto child_ptr = FlatVector::GetData<float>(*child_vec);

	auto n_queries = input.size();

	auto labels = unique_ptr<faiss::idx_t[]>(new faiss::idx_t[n_queries * n_results]);
	auto distances = unique_ptr<float[]>(new float[n_queries * n_results]);

	// the actual search woo
	indexes[key].index->search((faiss::idx_t)n_queries, child_ptr, n_results, distances.get(), labels.get());

	ListVector::SetListSize(output, n_queries * n_results);
	auto list_ptr = ListVector::GetData(output);
	auto &result_struct_vector = ListVector::GetEntry(output);
	auto &struct_entries = StructVector::GetEntries(result_struct_vector);
	auto rank_ptr = FlatVector::GetData<int32_t>(*struct_entries[0]);
	auto label_ptr = FlatVector::GetData<int64_t>(*struct_entries[1]);
	auto distance_ptr = FlatVector::GetData<float>(*struct_entries[2]);

	idx_t list_offset = 0;

	for (idx_t row_idx = 0; row_idx < n_queries; row_idx++) {
		list_ptr[row_idx].length = n_results;
		list_ptr[row_idx].offset = list_offset;

		for (idx_t res_idx = 0; res_idx < n_results; res_idx++) {
			rank_ptr[list_offset + res_idx] = (int32_t)res_idx;
			label_ptr[list_offset + res_idx] = labels[row_idx * n_results + res_idx];
			distance_ptr[list_offset + res_idx] = distances[row_idx * n_results + res_idx];
		}
		list_offset += n_results;
	}
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
		TableFunction add_function("faiss_add", {LogicalType::VARCHAR, LogicalType::TABLE}, nullptr, AddBind);
		add_function.in_out_function = AddFunction;
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
		TableFunction create_func("faiss_destroy", {LogicalType::VARCHAR}, DestroyFunction, DestroyBind);
		CreateTableFunctionInfo create_info(create_func);
		catalog.CreateTableFunction(*con.context, &create_info);
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

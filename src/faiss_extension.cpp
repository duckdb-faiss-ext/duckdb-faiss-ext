#include "faiss_extension.hpp"

#include "duckdb/common/error_data.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/helper.hpp"
#include "duckdb/common/optional_ptr.hpp"
#include "duckdb/common/shared_ptr.hpp"
#include "duckdb/common/typedefs.hpp"
#include "duckdb/common/types.hpp"
#include "duckdb/common/types/data_chunk.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/common/unique_ptr.hpp"
#include "duckdb/common/vector.hpp"
#include "duckdb/execution/execution_context.hpp"
#include "duckdb/execution/expression_executor.hpp"
#include "duckdb/function/function.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/connection.hpp"
#include "duckdb/main/database.hpp"
#include "duckdb/main/prepared_statement.hpp"
#include "duckdb/parallel/thread_context.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"
#include "duckdb/planner/binder.hpp"
#include "duckdb/planner/expression.hpp"
#include "duckdb/storage/object_cache.hpp"
#include "faiss/Index.h"
#include "faiss/IndexHNSW.h"
#include "faiss/IndexIDMap.h"
#include "faiss/IndexIVF.h"
#include "faiss/MetricType.h"
#include "faiss/impl/FaissException.h"
#include "faiss/impl/HNSW.h"
#include "faiss/impl/IDSelector.h"
#include "faiss/index_factory.h"
#include "faiss/index_io.h"
#include "gpu.hpp"
#include "index.hpp"
#include "maputils.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#define DUCKDB_EXTENSION_MAIN

namespace duckdb {

using metric_type_map_t = case_insensitive_map_t<faiss::MetricType>;

extern const metric_type_map_t LookupTable;

const metric_type_map_t LookupTable = {
    {"INNER_PRODUCT", faiss::METRIC_INNER_PRODUCT},
    {"L2", faiss::METRIC_L2},
    {"L1", faiss::METRIC_L1},
    {"Linf", faiss::METRIC_Linf},
    {"Lp", faiss::METRIC_Lp},
    {"Canberra", faiss::METRIC_Canberra},
    {"BrayCurtis", faiss::METRIC_BrayCurtis},
    {"JensenShannon", faiss::METRIC_JensenShannon},
    {"Jaccard", faiss::METRIC_Jaccard},
};

// Create function
struct CreateFunctionData : public TableFunctionData {
	string key;
	int dimension = 0;
	string description;
	faiss::MetricType metricType;
	shared_ptr<Vector> indexParams = nullptr;
	uint64_t paramCount = 0;
};

std::unordered_map<std::string, CreateParamHandler> FaissExtension::create_param_handlers;

void FaissExtension::RegisterCreateParameter(const std::string &key, CreateParamHandler handler) {
	create_param_handlers[key] = handler;
}

void FaissExtension::RegisterMetricType() {
	FaissExtension::RegisterCreateParameter("metric_type", [](CreateFunctionData &result, const Value &val) {
		auto it = LookupTable.find(val.GetValue<std::string>());
		if (it == LookupTable.end()) {
			throw InvalidInputException("Unknown metric type: %s", val.GetValue<std::string>());
		}
		result.metricType = it->second;
	});
}

static unique_ptr<FunctionData> CreateBind(ClientContext &, TableFunctionBindInput &input,
                                           vector<LogicalType> &return_types, vector<string> &names) {
	auto result = make_uniq<CreateFunctionData>();
	return_types.emplace_back(LogicalType::BOOLEAN);
	names.emplace_back("Success");

	result->key = input.inputs[0].ToString();
	result->dimension = input.inputs[1].GetValue<int>();
	result->description = input.inputs[2].ToString();
	result->metricType = faiss::METRIC_INNER_PRODUCT;

	if (input.inputs.size() == 4) {
		std::tie(result->indexParams, result->paramCount) = mapFromValue(input.inputs[3]);
		D_ASSERT(result->indexParams);
	}

	for (const auto &kv : input.named_parameters) {
		auto it = FaissExtension::create_param_handlers.find(kv.first);
		if (it != FaissExtension::create_param_handlers.end()) {
			it->second(*result, kv.second);
		} else {
			throw InvalidInputException("Unknown named parameter: %s", kv.first);
		}
	}
	return std::move(result);
}

faiss::Index *setIndexParameters(faiss::Index *index, Vector *userParams, uint64_t paramCount) {
	if (!userParams || paramCount == 0) {
		return index;
	}
	faiss::IndexIDMap *idmap = dynamic_cast<faiss::IndexIDMap *>(index);
	if (idmap) {
		idmap->index = setIndexParameters(idmap->index, userParams, paramCount);
		return idmap;
	}

	faiss::IndexHNSW *hnsw = dynamic_cast<faiss::IndexHNSW *>(index);
	if (hnsw) {
		unique_ptr<faiss::SearchParametersHNSW> searchParams = make_uniq<faiss::SearchParametersHNSW>();
		string efconstruction = getUserParamValue(*userParams, paramCount, "efConstruction");
		if (efconstruction != "") {
			hnsw->hnsw.efConstruction = std::stoi(efconstruction);
		}
		return hnsw;
	}

	return index;
}

static void CreateFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &) {
	auto &bind_data = data_p.bind_data->Cast<CreateFunctionData>();
	auto &object_cache = ObjectCache::GetObjectCache(context);

	if (object_cache.Get<IndexEntry>(bind_data.key)) {
		throw InvalidInputException("Index %s already exists.", bind_data.key);
	}

	faiss::Index *index =
	    faiss::index_factory(bind_data.dimension, bind_data.description.c_str(), bind_data.metricType);
	index = setIndexParameters(index, bind_data.indexParams.get(), bind_data.paramCount);
	auto entry = make_shared_ptr<IndexEntry>();
	entry->index = unique_ptr<faiss::Index>(index);
	entry->needs_training = !entry->index.get()->is_trained;
	entry->faiss_lock = unique_ptr<std::mutex>(new std::mutex());
	entry->add_lock = unique_ptr<std::mutex>(new std::mutex());

	object_cache.Put(bind_data.key, std::move(entry));
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

	auto entry = make_shared_ptr<IndexEntry>();
	entry->index = unique_ptr<faiss::Index>(faiss::read_index(bind_data.filename.c_str()));
	entry->needs_training = !entry->index.get()->is_trained;
	entry->faiss_lock = unique_ptr<std::mutex>(new std::mutex());
	entry->add_lock = unique_ptr<std::mutex>(new std::mutex());
	entry->isMutable = entry->needs_training;

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

	object_cache.Delete(bind_data.key);
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

// Manual train function

struct MTrainState : public GlobalTableFunctionState {
	// This keeps track of how many threads are currently adding, this is to make sure that we
	// only train/push to faiss when there are no threads adding more data. Does not guarantee
	// that all data has been added, as some threads may just have not been started yet
	std::atomic_uint64_t currently_adding;
	unique_ptr<std::mutex> add_lock = unique_ptr<std::mutex>(new std::mutex());
	// Store chunks for the add function (which can be done in parallel)
	// to be added all at once at the end
	vector<float> add_data;
};

struct MTrainData : TableFunctionData {
	string key;
};

static unique_ptr<FunctionData> MTrainBind(ClientContext &, TableFunctionBindInput &input,
                                           vector<LogicalType> &return_types, vector<string> &names) {
	auto bind_data = make_uniq<MTrainData>();

	return_types.emplace_back(LogicalType::BOOLEAN);
	names.emplace_back("Success");

	bind_data->key = input.inputs[1].ToString();

	return std::move(bind_data);
}

static unique_ptr<GlobalTableFunctionState> MTrainGlobalInit(ClientContext &context, TableFunctionInitInput &input) {
	return make_uniq<MTrainState>();
}

static unique_ptr<LocalTableFunctionState> MTrainLocalInit(ExecutionContext &context, TableFunctionInitInput &data_p,
                                                           GlobalTableFunctionState *global_state) {
	MTrainState &state = global_state->Cast<MTrainState>();
	state.currently_adding++;
	return make_uniq<LocalTableFunctionState>();
}

static OperatorResultType MTrainFunction(ExecutionContext &context, TableFunctionInput &data_p, DataChunk &input,
                                         DataChunk &) {
	MTrainState &state = data_p.global_state->Cast<MTrainState>();

	auto bind_data = data_p.bind_data->Cast<MTrainData>();
	auto &object_cache = ObjectCache::GetObjectCache(context.client);
	auto entry_ptr = object_cache.Get<IndexEntry>(bind_data.key);
	if (!entry_ptr) {
		throw InvalidInputException("Could not find index %s.", bind_data.key);
	}
	auto &entry = *entry_ptr;

	if (!entry.isMutable) {
		throw InvalidInputException(
		    "Attempted to train to an immutable index. Indexes are marked immutable if they are "
		    "loaded from disk and don't need training.");
	}

	auto data_elements = input.size() * entry.index->d;

	auto child_vec = ListVectorToFaiss(context.client, input.data[0], input.size(), entry.index->d);
	auto child_ptr = FlatVector::GetData<float>(*child_vec);

	state.add_lock.get()->lock();
	size_t original_data_size = state.add_data.size();
	state.add_data.resize(original_data_size + data_elements);
	memcpy(&state.add_data.data()[original_data_size], child_ptr, data_elements * sizeof(float));
	state.add_lock.get()->unlock();

	return OperatorResultType::NEED_MORE_INPUT;
}

static OperatorFinalizeResultType MTrainFinaliseFunction(ExecutionContext &context, TableFunctionInput &data_p,
                                                         DataChunk &) {
	MTrainState &state = data_p.global_state->Cast<MTrainState>();

	auto bind_data = data_p.bind_data->Cast<MTrainData>();
	auto &object_cache = ObjectCache::GetObjectCache(context.client);
	auto entry_ptr = object_cache.Get<IndexEntry>(bind_data.key);
	if (!entry_ptr) {
		throw InvalidInputException("Could not find index %s.", bind_data.key);
	}

	auto &entry = *entry_ptr;

	state.add_lock.get()->lock();
	state.currently_adding--;
	if (state.currently_adding != 0) {
		state.add_lock.get()->unlock();
		return OperatorFinalizeResultType::FINISHED;
	}
	state.add_lock.get()->unlock();

	if (state.add_data.size() == 0) {
		return OperatorFinalizeResultType::FINISHED;
	}

	entry.faiss_lock.get()->lock();
	try {
		entry.index->train((faiss::idx_t)state.add_data.size() / entry.index->d, &state.add_data[0]);
	} catch (faiss::FaissException exception) {
		entry.faiss_lock.get()->unlock();
		std::string msg = exception.msg;
		if (msg.find("should be at least as large as number of clusters") != std::string::npos) {
			throw InvalidInputException(
			    "Index needs to be trained, but amount of datapoints is too small. Considere adding more data. (" +
			        msg + ")",
			    bind_data.key);
		} else {
			throw InvalidInputException("Error occured while training index: %s", msg);
		}
	}

	entry.faiss_lock.get()->unlock();
	// Future calls to add to the index won't train the index anymore.
	// They wil directly add to the index.
	entry.needs_training = false;
	return OperatorFinalizeResultType::FINISHED;
}

// Adding function

struct AddData : TableFunctionData {
	string key;
};

static unique_ptr<FunctionData> AddBind(ClientContext &context, TableFunctionBindInput &input,
                                        vector<LogicalType> &return_types, vector<string> &names) {
	auto bind_data = make_uniq<AddData>();

	return_types.emplace_back(LogicalType::BOOLEAN);
	names.emplace_back("Success");

	bind_data->key = input.inputs[1].ToString();

	auto &object_cache = ObjectCache::GetObjectCache(context);
	auto entry_ptr = object_cache.Get<IndexEntry>(bind_data->key);
	if (!entry_ptr) {
		throw InvalidInputException("Could not find index %s.", bind_data->key);
	}
	if (entry_ptr.get()->custom_labels == UNDECIDED) {
		if (input.input_table_names.size() == 2) { // we have labels
			entry_ptr.get()->custom_labels = TRUE;
		} else {
			entry_ptr.get()->custom_labels = FALSE;
		}
	} else {
		if (input.input_table_names.size() == 2 && entry_ptr.get()->custom_labels == FALSE) {
			throw InvalidInputException(
			    "Tried to insert data with labels, when index was previously added without labels. "
			    "Cannot mix index data with and without labels");
		} else if (input.input_table_names.size() == 1 && entry_ptr.get()->custom_labels == TRUE) {
			throw InvalidInputException(
			    "Tried to insert data without labels, when index was previously added with labels. "
			    "Cannot mix index data with and without labels");
		}
	}

	return std::move(bind_data);
}

static unique_ptr<GlobalTableFunctionState> AddGlobalInit(ClientContext &context, TableFunctionInitInput &input) {
	return make_uniq<GlobalTableFunctionState>();
}

static unique_ptr<LocalTableFunctionState> AddLocalInit(ExecutionContext &context, TableFunctionInitInput &data_p,
                                                        GlobalTableFunctionState *global_state) {
	auto bind_data = data_p.bind_data->Cast<AddData>();
	auto &object_cache = ObjectCache::GetObjectCache(context.client);
	auto entry_ptr = object_cache.Get<IndexEntry>(bind_data.key);
	if (!entry_ptr) {
		throw InvalidInputException("Could not find index %s.", bind_data.key);
	}
	auto &entry = *entry_ptr;
	entry.currently_adding++;
	return make_uniq<LocalTableFunctionState>();
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

	if (!entry.isMutable) {
		throw InvalidInputException("Attempted to add to an immutable index. Indexes are marked immutable if they are "
		                            "loaded from disk and don't need training.");
	}

	auto data_elements = input.size() * entry.index->d;
	idx_t vector_count = input.size();

	auto child_vec = ListVectorToFaiss(context.client, entry.custom_labels == TRUE ? input.data[1] : input.data[0],
	                                   input.size(), entry.index->d);
	auto child_ptr = FlatVector::GetData<float>(*child_vec);

	Vector label_cast_vec(LogicalType::BIGINT);
	faiss::idx_t *label_ptr;
	if (entry.custom_labels == TRUE) {
		VectorOperations::Cast(context.client, input.data[0], label_cast_vec, input.size());
		label_ptr = FlatVector::GetData<faiss::idx_t>(label_cast_vec);
	}

	// If we do not need to do any training, we can add all the data instantly!
	if (!entry.needs_training) {
		entry.faiss_lock.get()->lock();
		// Yay error handling!
		try {
			if (entry.custom_labels == TRUE) {
				entry.index->add_with_ids((faiss::idx_t)input.size(), child_ptr, label_ptr);
			} else if (entry.custom_labels == FALSE) {
				entry.index->add((faiss::idx_t)input.size(), child_ptr);
			}
		} catch (faiss::FaissException exception) {
			entry.faiss_lock.get()->unlock();

			// This should reset if no data was added for some reason.
			if (entry.custom_labels == TRUE && entry.index->ntotal == 0) {
				entry_ptr.get()->custom_labels = UNDECIDED;
			}

			std::string msg = exception.msg;
			if (msg.find("add_with_ids not implemented for this type of index") != std::string::npos) {
				throw InvalidInputException("Unable to add data: This type of index does not support adding with IDs. "
				                            "Consider prefixing the index string with IDMap when creating the index.");
			}

			throw InvalidInputException("Unable to add data: %s", exception.what());
		}
		entry.faiss_lock.get()->unlock();
		return OperatorResultType::NEED_MORE_INPUT;
	}

	entry.add_lock.get()->lock();
	size_t original_data_size = entry.add_data.size();
	entry.add_data.resize(original_data_size + data_elements);
	memcpy(&entry.add_data.data()[original_data_size], child_ptr, data_elements * sizeof(float));
	if (entry.custom_labels == TRUE) {
		size_t original_label_size = entry.add_labels.size();
		entry.add_labels.resize(original_label_size + vector_count);
		memcpy(&entry.add_labels.data()[original_label_size], label_ptr, input.size() * sizeof(long));
	}
	entry.size += vector_count;
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
	entry.currently_adding--;
	if (entry.currently_adding != 0) {
		entry.add_lock.get()->unlock();
		return OperatorFinalizeResultType::FINISHED;
	}
	size_t total_elements = entry.size;
	size_t added_elements = entry.added;
	if (entry.added == total_elements) {
		entry.add_lock.get()->unlock();
		return OperatorFinalizeResultType::FINISHED;
	}
	// We will mark these as added already, so concurrent calls to
	// AddFinaliseFunction already these are taken care of
	entry.added = total_elements;

	entry.add_lock.get()->unlock();

	if (entry.add_data.size() == 0) {
		return OperatorFinalizeResultType::FINISHED;
	}

	entry.faiss_lock.get()->lock();
	try {
		entry.index->train((faiss::idx_t)total_elements, &entry.add_data[0]);
	} catch (faiss::FaissException exception) {
		// This should reset if no data was added for some reason.
		if (entry.custom_labels == TRUE && entry.index->ntotal == 0) {
			entry_ptr.get()->custom_labels = UNDECIDED;
		}

		entry.faiss_lock.get()->unlock();
		std::string msg = exception.msg;
		if (msg.find("should be at least as large as number of clusters") != std::string::npos) {
			throw InvalidInputException(
			    "Index %s needs to be trained, but amount of datapoints is too small. Considere adding more data. (" +
			        msg + ")",
			    bind_data.key);
		} else {
			throw InvalidInputException("Error occured while training index: %s", msg);
		}
	}

	// We only add data that havn't been added yet, so we skip the data already added
	faiss::idx_t new_element_count = total_elements - added_elements;
	float *new_vector_data = &entry.add_data[added_elements * entry.index->d];
	if (entry.custom_labels == TRUE) {
		faiss::idx_t *new_label_data = &entry.add_labels[added_elements];
		entry.index->add_with_ids(new_element_count, new_vector_data, new_label_data);
	} else {
		entry.index->add(new_element_count, new_vector_data);
	}

	entry.faiss_lock.get()->unlock();

	return OperatorFinalizeResultType::FINISHED;
}

// Search functions and helpers

// Searches the faiss index contained in the IndexEntry using the given queries and inputdata and search params. The
// results are stored in the outputvector as a struct-type of the form {rank, id, distance}.
void searchIntoVector(ClientContext &ctx, IndexEntry &entry, Vector inputdata, size_t nQueries, size_t nResults,
                      faiss::SearchParameters *searchParams, Vector &output) {
	unique_ptr<Vector> child_vec = ListVectorToFaiss(ctx, inputdata, nQueries, entry.index->d);
	auto child_ptr = FlatVector::GetData<float>(*child_vec);

	unique_ptr<faiss::idx_t[]> labels = unique_ptr<faiss::idx_t[]>(new faiss::idx_t[nQueries * nResults]);
	unique_ptr<float[]> distances = unique_ptr<float[]>(new float[nQueries * nResults]);

	entry.faiss_lock.get()->lock(); //  this should be a readlock once c++17 is supported
	try {
		entry.index->search((faiss::idx_t)nQueries, child_ptr, nResults, distances.get(), labels.get(), searchParams);
	} catch (faiss::FaissException exception) {
		entry.faiss_lock.get()->unlock();
		std::string msg = exception.msg;
		throw InvalidInputException("Error occured while searching: %s", msg);
	}

	entry.faiss_lock.get()->unlock();

	ListVector::SetListSize(output, nQueries * nResults);
	ListVector::Reserve(output, nQueries * nResults);
	list_entry_t *list_ptr = ListVector::GetData(output);
	Vector &result_struct_vector = ListVector::GetEntry(output);
	vector<unique_ptr<Vector>> &struct_entries = StructVector::GetEntries(result_struct_vector);
	int *rank_ptr = FlatVector::GetData<int32_t>(*struct_entries[0]);
	int64_t *label_ptr = FlatVector::GetData<int64_t>(*struct_entries[1]);
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

vector<shared_ptr<faiss::SearchParameters>> innerCreateSearchParameters(faiss::Index *index,
                                                                        faiss::IDSelector *selector, Vector *userParams,
                                                                        uint64_t paramCount, string prefix) {
	faiss::IndexIDMap *idmap = dynamic_cast<faiss::IndexIDMap *>(index);
	if (idmap) {
		return innerCreateSearchParameters(idmap->index, selector, userParams, paramCount, prefix);
	}
	faiss::IndexIVF *ivf = dynamic_cast<faiss::IndexIVF *>(index);
	if (ivf) {
		shared_ptr<faiss::SearchParametersIVF> searchParams = make_shared_ptr<faiss::SearchParametersIVF>();
		searchParams->sel = selector;
		vector<shared_ptr<faiss::SearchParameters>> ret =
		    innerCreateSearchParameters(ivf->quantizer, NULL, userParams, paramCount, prefix + "quantiser.");
		searchParams->quantizer_params = ret[0].get();
		// stoi can throw, we should catch and rethrow invalid input exception.
		string nprobe = getUserParamValue(*userParams, paramCount, prefix + "nprobe");
		if (nprobe != "") {
			searchParams->nprobe = std::stoi(nprobe);
		}
		ret.insert(ret.begin(), searchParams);
		return ret;
	}

	void *hnsw = dynamic_cast<faiss::IndexHNSW *>(index);
	if (hnsw) {
		shared_ptr<faiss::SearchParametersHNSW> searchParams = make_shared_ptr<faiss::SearchParametersHNSW>();
		searchParams->sel = selector;
		// stoi can throw, we should catch and rethrow invalid input exception.
		string efSearch = getUserParamValue(*userParams, paramCount, prefix + "efSearch");
		if (efSearch != "") {
			searchParams->efSearch = std::stoi(efSearch);
		}

		return vector<shared_ptr<faiss::SearchParameters>>(1, searchParams);
	}

	void *pq = dynamic_cast<faiss::IndexPQ *>(index);
	if (pq) {
		shared_ptr<faiss::SearchParametersPQ> searchParams = make_shared_ptr<faiss::SearchParametersPQ>();
		return vector<shared_ptr<faiss::SearchParameters>>(1, searchParams);
	}

#ifdef DDBF_ENABLE_GPU
	vector<shared_ptr<faiss::SearchParameters>> gpuparams =
	    innerCreateSearchParametersGPU(index, selector, userParams, paramCount, prefix);
	if (gpuparams.size() != 0) {
		return gpuparams;
	}
#endif

	shared_ptr<faiss::SearchParameters> searchParams = make_shared_ptr<faiss::SearchParameters>();
	searchParams->sel = selector;
	return vector<shared_ptr<faiss::SearchParameters>>(1, searchParams);
}

// The return type is a vector to make sure that the sub-search parameters are kept alive
vector<shared_ptr<faiss::SearchParameters>> createSearchParameters(faiss::Index *index, faiss::IDSelector *selector,
                                                                   Vector *userParams, uint64_t paramCount) {
	return innerCreateSearchParameters(index, selector, userParams, paramCount, "");
}

void ProcessSelectionvector(unique_ptr<DataChunk> &chunk, std::vector<uint8_t> &output) {
	idx_t size = chunk->size();
	if (size == 0) {
		return;
	}
	Vector data = chunk->data[0];
	Vector ids = chunk->data[1];
	uint8_t *__restrict dataBytes = data.GetData();

	bool sequential;
	uint64_t start;
	uint64_t max;
	switch (ids.GetVectorType()) {
	case VectorType::SEQUENCE_VECTOR: {
		int64_t sstart, increment, sequence_count;
		SequenceVector::GetSequence(ids, sstart, increment, sequence_count);
		start = sstart;
		max = start + increment * sequence_count;
		sequential = increment == 1 && start >= 0;
		break;
	}
	default:
		ids.Flatten(size); // TODO: we can do without the flatten!
		uint64_t *__restrict idBytes = (uint64_t *)ids.GetData();

		uint64_t previous = idBytes[0] - 1;
		sequential = true;
		start = idBytes[0];
		max = 0;
		for (int i = 0; i < size; i++) {
			max = MaxValue(max, idBytes[i]);
			sequential &= idBytes[i] == previous + 1;
			previous = idBytes[i];
		}
	}

	if (output.size() <= max / 8) {
		output.resize(max / 8 + 1);
	}

	// If the input is not sequential or alligned, use the slow path
	if (!sequential) {
		uint64_t *__restrict idBytes = (uint64_t *)ids.GetData();
		for (int i = 0; i < size; i++) {
			uint64_t id = idBytes[i];
			int arrIndex = id / 8;
			int u8Index = id % 8;

			output[arrIndex] = output[arrIndex] | (dataBytes[i] << u8Index);
		}
	} else {
		int i = 0;
		int id = start;
		for (; id % 8 != 0;) {
			output[id / 8] = output[id / 8] | (dataBytes[i] << (id % 8));
			i++;
			id++;
		}
		int arrIndex = id / 8;
		if (size >= 8) {
			for (; i < size - 8;) {
				output[arrIndex] = (dataBytes[i + 0] << 0) | (dataBytes[i + 1] << 1) | (dataBytes[i + 2] << 2) |
				                   (dataBytes[i + 3] << 3) | (dataBytes[i + 4] << 4) | (dataBytes[i + 5] << 5) |
				                   (dataBytes[i + 6] << 6) | (dataBytes[i + 7] << 7);
				i += 8;
				arrIndex += 1;
			}
		}
		id = arrIndex * 8;
		for (; i < size;) {
			output[arrIndex] = output[arrIndex] | (dataBytes[i] << (id % 8));
			i++;
			id++;
		}
	}
}

void ProcessIncludeSet(unique_ptr<DataChunk> &chunk, std::vector<faiss::idx_t> &output) {
	Vector ids = chunk->data[0];
	uint64_t *__restrict idBytes = (uint64_t *)ids.GetData();

	idx_t target = output.size();
	output.resize(output.size() + chunk->size());
	if (sizeof(faiss::idx_t) == sizeof(uint64_t)) {
		memcpy(&output[target], idBytes, chunk->size() * sizeof(faiss::idx_t));
	} else {
		idx_t size = chunk->size();
		for (idx_t i = 0; i < size; i++) {
			output[target + i] = idBytes[i];
		}
	}
}

struct SelState : public GlobalTableFunctionState {
	// This keeps track of how many threads are currently adding, this is to make sure that we
	// only train/push to faiss when there are no threads adding more data. Does not guarantee
	// that all data has been added, as some threads may just have not been started yet
	std::atomic_uint64_t currently_adding;
	unique_ptr<std::mutex> lock = unique_ptr<std::mutex>(new std::mutex());
	vector<uint8_t> mask;
	unique_ptr<DataChunk> chunk;
};

struct SelData : public TableFunctionData {
	string key;
};

static unique_ptr<TableFunctionData> SelBind(ClientContext &context, TableFunctionBindInput &input,
                                             vector<LogicalType> &return_types, vector<string> &names) {
	auto bind_data = make_uniq<SelData>();

	return_types.emplace_back(LogicalType::BOOLEAN);
	names.emplace_back("Success");

	bind_data->key = input.inputs[2].ToString();

	auto &object_cache = ObjectCache::GetObjectCache(context);
	auto entry_ptr = object_cache.Get<IndexEntry>(bind_data->key);
	if (!entry_ptr) {
		throw InvalidInputException("Could not find index %s.", bind_data->key);
	}

	return std::move(bind_data);
}

static unique_ptr<GlobalTableFunctionState> SelGlobalInit(ClientContext &context, TableFunctionInitInput &input) {
	unique_ptr<SelState> state = make_uniq<SelState>();
	state->chunk = make_uniq<DataChunk>();
	state->chunk->Initialize(context, vector<LogicalType> {LogicalType::UTINYINT, LogicalType::BIGINT});

	return std::move(state);
}

static unique_ptr<LocalTableFunctionState> SelLocalInit(ExecutionContext &context, TableFunctionInitInput &data_p,
                                                        GlobalTableFunctionState *global_state) {
	SelState &state = global_state->Cast<SelState>();
	state.currently_adding++;
	state.lock->lock();

	return make_uniq<LocalTableFunctionState>();
}

static OperatorResultType SelFunction(ExecutionContext &context, TableFunctionInput &data_p, DataChunk &input,
                                      DataChunk &) {
	SelState *state = (SelState *)data_p.global_state.get();
	unique_ptr<DataChunk> chunk = make_uniq<DataChunk>();
	state->chunk->ReferenceColumns(input, vector<column_t> {0, 1});
	ProcessSelectionvector(state->chunk, state->mask);

	return OperatorResultType::NEED_MORE_INPUT;
}

static OperatorFinalizeResultType SelFinaliseFunction(ExecutionContext &context, TableFunctionInput &data_p,
                                                      DataChunk &) {
	SelState &state = data_p.global_state->Cast<SelState>();

	auto bind_data = data_p.bind_data->Cast<AddData>();
	auto &object_cache = ObjectCache::GetObjectCache(context.client);
	auto entry_ptr = object_cache.Get<IndexEntry>(bind_data.key);
	if (!entry_ptr) {
		throw InvalidInputException("Could not find index %s.", bind_data.key);
	}

	state.currently_adding--;
	if (state.currently_adding != 0) {
		state.lock.get()->unlock();
		return OperatorFinalizeResultType::FINISHED;
	}

	entry_ptr->mask_tmp = state.mask;
	state.lock->unlock();
	return OperatorFinalizeResultType::FINISHED;
}

void SearchFunction(DataChunk &input, ExpressionState &state, Vector &output) {
	string key = input.data[0].GetValue(0).ToString();

	auto &object_cache = ObjectCache::GetObjectCache(state.GetContext());
	auto entry_ptr = object_cache.Get<IndexEntry>(key);
	if (!entry_ptr) {
		throw InvalidInputException("Could not find index %s.", key);
	}

	input.data[2].Flatten(input.size());
	size_t nQueries = FlatVector::Validity(input.data[2]).CountValid(input.size());
	size_t nResults = input.data[1].GetValue(0).GetValue<int32_t>();
	shared_ptr<Vector> userParams = nullptr;
	uint64_t paramCount = 0;
	if (input.data.size() == 4) {
		std::tie(userParams, paramCount) = mapFromValue(input.data[3].GetValue(0));
	}

	vector<shared_ptr<faiss::SearchParameters>> searchParams =
	    createSearchParameters(entry_ptr->index.get(), NULL, userParams.get(), paramCount);

	searchIntoVector(state.GetContext(), *entry_ptr, input.data[2], nQueries, nResults, searchParams[0].get(), output);
}

void SearchFunctionFilter(DataChunk &input, ExpressionState &state, Vector &output) {
	auto &object_cache = ObjectCache::GetObjectCache(state.GetContext());
	auto key = input.data[0].GetValue(0).ToString();

	auto entry_ptr = object_cache.Get<IndexEntry>(key);
	if (!entry_ptr) {
		throw InvalidInputException("Could not find index %s.", key);
	}
	auto &entry = *entry_ptr;

	// Once possible use prepared statements, currently not possible to use variables for tables (SELECT *
	// FROM $1 doesnt parse) use std::format in c++20, this is really ugly
	std::stringstream ss;
	ss << "CALL __faiss_create_mask((SELECT CAST(" << input.data[3].GetValue(0).GetValue<string>()
	   << " AS UTINYINT), CAST(" << input.data[4].GetValue(0).GetValue<string>() << " AS BIGINT) from "
	   << input.data[5].GetValue(0).GetValue<string>() << "), " << key << ") ";

	string filterExpression = ss.str();

	shared_ptr<DatabaseInstance> db = state.GetContext().db;
	Connection con = Connection(*db);
	unique_ptr<QueryResult> result = con.SendQuery(filterExpression);

	if (result->HasError()) {
		throw InvalidInputException("uable to execute filter query: %s", result->GetError());
	}
	unique_ptr<DataChunk> chunk = unique_ptr<DataChunk>();
	ErrorData error;
	while (result->TryFetch(chunk, error) && chunk) {
	}

	// create selector
	faiss::IDSelectorBitmap selector = faiss::IDSelectorBitmap(entry_ptr->mask_tmp.size(), entry_ptr->mask_tmp.data());

	// === normal search ===
	size_t nQueries = input.size();
	size_t nResults = input.data[1].GetValue(0).GetValue<int32_t>();
	shared_ptr<Vector> userParams = nullptr;
	uint64_t paramCount = 0;
	if (input.data.size() == 7) {
		std::tie(userParams, paramCount) = mapFromValue(input.data[6].GetValue(0));
	}
	vector<shared_ptr<faiss::SearchParameters>> searchParams =
	    createSearchParameters(entry.index.get(), &selector, userParams.get(), paramCount);
	searchIntoVector(state.GetContext(), entry, input.data[2], nQueries, nResults, searchParams[0].get(), output);
}

void SearchFunctionFilterSet(DataChunk &input, ExpressionState &state, Vector &output) {
	auto &object_cache = ObjectCache::GetObjectCache(state.GetContext());
	auto key = input.data[0].GetValue(0).ToString();

	auto entry_ptr = object_cache.Get<IndexEntry>(key);
	if (!entry_ptr) {
		throw InvalidInputException("Could not find index %s.", key);
	}
	auto &entry = *entry_ptr;

	// Once possible use prepared statements, currently not possible to use variables for tables (SELECT *
	// FROM $1 doesnt parse) use std::format in c++20, this is really ugly
	std::stringstream ss;
	// Unlike the normal nomenclature, (u)int1 means int of 1 byte, and (u)int8 means 8 bytes.
	ss << "SELECT CAST(" << input.data[4].GetValue(0).GetValue<string>() << " AS BIGINT) from "
	   << input.data[5].GetValue(0).GetValue<string>() << " WHERE " << input.data[3].GetValue(0).GetValue<string>();

	string filterExpression = ss.str();

	shared_ptr<DatabaseInstance> db = state.GetContext().db;
	shared_ptr<ClientContext> subcommection = make_shared_ptr<ClientContext>(db);
	unique_ptr<QueryResult> result = subcommection->Query(filterExpression, false);

	if (result->HasError()) {
		throw InvalidInputException("uable to execute filter query: %s", result->GetError());
	}
	vector<faiss::idx_t> mask = vector<faiss::idx_t>();
	unique_ptr<DataChunk> chunk = unique_ptr<DataChunk>();
	ErrorData error;
	while (result->TryFetch(chunk, error) && chunk) {
		ProcessIncludeSet(chunk, mask);
	}

	// create selector
	faiss::IDSelectorBatch selector = faiss::IDSelectorBatch(mask.size(), mask.data());

	// === normal search ===
	size_t nQueries = input.size();
	size_t nResults = input.data[1].GetValue(0).GetValue<int32_t>();
	shared_ptr<Vector> userParams = nullptr;
	uint64_t paramCount = 0;
	if (input.data.size() == 7) {
		std::tie(userParams, paramCount) = mapFromValue(input.data[6].GetValue(0));
	}
	vector<shared_ptr<faiss::SearchParameters>> searchParams =
	    createSearchParameters(entry.index.get(), &selector, userParams.get(), paramCount);

	searchIntoVector(state.GetContext(), entry, input.data[2], nQueries, nResults, searchParams[0].get(), output);
}

// LoadInternal adds the faiss functions to the database
static void LoadInternal(DatabaseInstance &instance) {
	Connection con(instance);
	con.BeginTransaction();
	auto &catalog = Catalog::GetSystemCatalog(*con.context);

	{
		TableFunction create_func("faiss_create", {LogicalType::VARCHAR, LogicalType::INTEGER, LogicalType::VARCHAR},
		                          CreateFunction, CreateBind);
		create_func.named_parameters["metric_type"] = LogicalType::VARCHAR;
		CreateTableFunctionInfo create_info(create_func);
		catalog.CreateTableFunction(*con.context, &create_info);
	}

	{
		TableFunction create_func("faiss_create_params",
		                          {LogicalType::VARCHAR, LogicalType::INTEGER, LogicalType::VARCHAR,
		                           LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR)},
		                          CreateFunction, CreateBind);
		CreateTableFunctionInfo create_info(create_func);
		catalog.CreateTableFunction(*con.context, &create_info);
	}
#ifdef DDBF_ENABLE_GPU
	{
		TableFunction create_func("faiss_to_gpu", {LogicalType::VARCHAR, LogicalType::INTEGER}, MoveToGPUFunction,
		                          MoveToGPUBind);
		CreateTableFunctionInfo create_info(create_func);
		catalog.CreateTableFunction(*con.context, &create_info);
	}
#endif
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

	{
		TableFunction create_func("faiss_destroy", {LogicalType::VARCHAR}, DestroyFunction, DestroyBind);
		CreateTableFunctionInfo create_info(create_func);
		catalog.CreateTableFunction(*con.context, &create_info);
	}

	{
		TableFunction manual_train_function("faiss_manual_train", {LogicalType::TABLE, LogicalType::VARCHAR}, nullptr,
		                                    MTrainBind, MTrainGlobalInit, MTrainLocalInit);
		manual_train_function.in_out_function = MTrainFunction;
		manual_train_function.in_out_function_final = MTrainFinaliseFunction;
		CreateTableFunctionInfo manual_train_info(manual_train_function);
		catalog.CreateTableFunction(*con.context, &manual_train_info);
	}

	{
		TableFunction add_function("faiss_add", {LogicalType::TABLE, LogicalType::VARCHAR}, nullptr, AddBind,
		                           AddGlobalInit, AddLocalInit);
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

		vector<LogicalType> parameters = {LogicalType::VARCHAR, LogicalType::INTEGER,
		                                  LogicalType::LIST(LogicalType::ANY)};

		ScalarFunction search_function("faiss_search", parameters, return_type, SearchFunction);
		CreateScalarFunctionInfo search_info(search_function);
		catalog.CreateFunction(*con.context, search_info);

		parameters.push_back(LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR));
		ScalarFunction search_function2("faiss_search", parameters, return_type, SearchFunction);
		CreateScalarFunctionInfo search_info_params(search_function2);
		search_info_params.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
		catalog.CreateFunction(*con.context, search_info_params);
	}

	{
		child_list_t<LogicalType> struct_children;
		struct_children.emplace_back("rank", LogicalType::INTEGER);
		struct_children.emplace_back("label", LogicalType::BIGINT);
		struct_children.emplace_back("distance", LogicalType::FLOAT);
		auto return_type = LogicalType::LIST(LogicalType::STRUCT(std::move(struct_children)));

		vector<LogicalType> parameters = {
		    LogicalType::VARCHAR, LogicalType::INTEGER, LogicalType::LIST(LogicalType::ANY),
		    LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR};

		ScalarFunction search_function("faiss_search_filter", parameters, return_type, SearchFunctionFilter);
		CreateScalarFunctionInfo search_info(search_function);
		catalog.CreateFunction(*con.context, search_info);

		parameters.push_back(LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR));
		ScalarFunction search_function_params("faiss_search_filter", parameters, return_type, SearchFunctionFilter);
		CreateScalarFunctionInfo search_info_params(search_function_params);
		search_info_params.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
		catalog.CreateFunction(*con.context, search_info_params);
	}

	{
		TableFunction add_function("__faiss_create_mask", {LogicalType::TABLE, LogicalType::VARCHAR}, nullptr, AddBind,
		                           SelGlobalInit, SelLocalInit);
		add_function.in_out_function = SelFunction;
		add_function.in_out_function_final = SelFinaliseFunction;
		CreateTableFunctionInfo add_info(add_function);
		catalog.CreateTableFunction(*con.context, &add_info);
	}

	{
		child_list_t<LogicalType> struct_children;
		struct_children.emplace_back("rank", LogicalType::INTEGER);
		struct_children.emplace_back("label", LogicalType::BIGINT);
		struct_children.emplace_back("distance", LogicalType::FLOAT);
		auto return_type = LogicalType::LIST(LogicalType::STRUCT(std::move(struct_children)));

		vector<LogicalType> parameters = {
		    LogicalType::VARCHAR, LogicalType::INTEGER, LogicalType::LIST(LogicalType::ANY),
		    LogicalType::VARCHAR, LogicalType::VARCHAR, LogicalType::VARCHAR};

		ScalarFunction search_function("faiss_search_filter_set", parameters, return_type, SearchFunctionFilterSet);
		CreateScalarFunctionInfo search_info(search_function);
		catalog.CreateFunction(*con.context, search_info);

		parameters.push_back(LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR));
		ScalarFunction search_function_filter_params("faiss_search_filter_set", parameters, return_type,
		                                             SearchFunctionFilterSet);
		CreateScalarFunctionInfo search_info_params(search_function_filter_params);
		search_info_params.on_conflict = OnCreateConflict::ALTER_ON_CONFLICT;
		catalog.CreateFunction(*con.context, search_info_params);
	}

	// manual training
	con.Commit();
}

void FaissExtension::Load(DuckDB &db) {
	RegisterMetricType();
	LoadInternal(*db.instance);
}

std::string FaissExtension::Name() {
	return "faiss";
}

std::string FaissExtension::Version() const {
	return "0.10.0";
}

} // namespace duckdb

extern "C" {

DUCKDB_EXTENSION_API void faiss_init(duckdb::DatabaseInstance &db) {
	duckdb::DuckDB db_wrapper(db);
	db_wrapper.LoadExtension<duckdb::FaissExtension>();
}

DUCKDB_EXTENSION_API const char *faiss_version() {
	return duckdb::DuckDB::LibraryVersion();
}
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif

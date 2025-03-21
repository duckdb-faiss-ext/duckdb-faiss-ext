#include "duckdb/function/function.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/storage/object_cache.hpp"
#include "faiss/gpu/GpuCloner.h"
#include "faiss/gpu/StandardGpuResources.h"
#include "index.hpp"

#include <string>

namespace duckdb {
// Move to GPU function
struct MoveToGPUDate : public TableFunctionData {
	string key;
	int device;
};

unique_ptr<FunctionData> MoveToGPUBind(ClientContext &, TableFunctionBindInput &input,
                                       vector<LogicalType> &return_types, vector<string> &names) {
	auto result = make_uniq<MoveToGPUDate>();
	return_types.emplace_back(LogicalType::BOOLEAN);
	names.emplace_back("Success");

	result->key = input.inputs[0].ToString();
	result->device = input.inputs[1].GetValue<int>();
	if (input.inputs.size() == 3) {
		// TODO: Add support for a map parameter for the GpuClonerOptions
		// std::tie(result->indexParams, result->paramCount) = mapFromValue(input.inputs[3]);
	}

	return std::move(result);
}

void MoveToGPUFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &) {
	auto &bind_data = data_p.bind_data->Cast<MoveToGPUDate>();
	auto &object_cache = ObjectCache::GetObjectCache(context);

	auto entry_ptr = object_cache.Get<IndexEntry>(bind_data.key);
	if (!entry_ptr) {
		throw InvalidInputException("Could not find index %s.", bind_data.key);
	}

	auto &entry = *entry_ptr;
	faiss::Index *index = &*entry.index;
	faiss::gpu::StandardGpuResources gpu_resources = faiss::gpu::StandardGpuResources();
	entry.faiss_lock.get()->lock();
	try {
		entry.index = unique_ptr<faiss::Index>(faiss::gpu::index_cpu_to_gpu(&gpu_resources, bind_data.device, index));
	} catch (faiss::FaissException exception) {
		entry.faiss_lock.get()->unlock();
		std::string msg = exception.msg;
		if (msg.find("This index type is not implemented") != std::string::npos) {
			throw InvalidInputException(
			    "The index type of %s is not supported on the GPU, please consider using a different index",
			    bind_data.key);
		} else if (msg.find("Invalid GPU device") != std::string::npos) {
			throw InvalidInputException("Invalid GPU index: %s", bind_data.key);
		} else {
			throw InvalidInputException("Error occured while training index: %s", msg);
		}
	}
	entry.faiss_lock.get()->unlock();
}

} // namespace duckdb
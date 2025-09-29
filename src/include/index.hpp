#include "duckdb/storage/object_cache.hpp"
#include "faiss/Index.h"

namespace duckdb {

enum LABELSTATE {
	UNDECIDED,
	FALSE,
	TRUE,
};

struct FaissIndexEntry : ObjectCacheEntry {
	unique_ptr<std::mutex>
	    faiss_lock; // c++11 doesnt have a shared_mutex, introduced in c++14. duckdb is build with c++11
	unique_ptr<faiss::Index> index;

	// This is true if the index needs training. When adding data
	// this is used to determine if it should re-train or not.
	bool needs_training = true;
	// isMutable reflects whether it is possible to add data to this
	// index while staying consistent.
	// If an index is loaded from disk, all meta-data is lost, so for safety
	// we assume that it is not possible to add any data, unless we know that it still
	// needs training (and thus doesnt have any data yet).needs_training
	bool isMutable = true;

	// Whether or not custom labels are used for this index.
	LABELSTATE custom_labels = UNDECIDED;

	// This keeps track of how many threads are currently adding, this is to make sure that we
	// only train/push to faiss when there are no threads adding more data. Does not guarantee
	// that all data has been added, as some threads may just have not been started yet
	std::atomic_uint64_t currently_adding;
	unique_ptr<std::mutex> add_lock;
	// Store chunks for the add function (which can be done in parallel)
	// to be added all at once at the end
	vector<float> add_data;
	vector<faiss::idx_t> add_labels;
	// Keeps track of how many vectors are there in total
	size_t size = 0;
	// keeps track of how many vectors have been added
	size_t added = 0;

	// Lock making sure only one thread uses the mask at a time
	unique_ptr<std::mutex> mask_lock = unique_ptr<std::mutex>(new std::mutex());
	// Temporary storage for selection mask
	vector<uint8_t> mask_tmp;

	static string ObjectType() {
		return "faiss_index";
	}

	string GetObjectType() override {
		return FaissIndexEntry::ObjectType();
	}
};
} // namespace duckdb

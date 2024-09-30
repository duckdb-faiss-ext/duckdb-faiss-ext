//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/execution/base_aggregate_hashtable.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/common/common.hpp"
#include "duckdb/common/types/row/tuple_data_layout.hpp"
#include "duckdb/common/types/vector.hpp"
#include "duckdb/execution/operator/aggregate/aggregate_object.hpp"

namespace duckdb {
class BufferManager;

class BaseAggregateHashTable {
public:
	BaseAggregateHashTable(ClientContext &context, Allocator &allocator, const vector<AggregateObject> &aggregates,
	                       vector<LogicalType> payload_types);
	virtual ~BaseAggregateHashTable() {
	}

protected:
	Allocator &allocator;
	BufferManager &buffer_manager;
	//! A helper for managing offsets into the data buffers
	TupleDataLayout layout;
	//! The types of the payload columns stored in the hashtable
	vector<LogicalType> payload_types;
};

} // namespace duckdb

//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/planner/operator/logical_copy_database.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/parser/parsed_data/copy_database_info.hpp"
#include "duckdb/planner/logical_operator.hpp"

namespace duckdb {

class LogicalCopyDatabase : public LogicalOperator {
public:
	static constexpr const LogicalOperatorType TYPE = LogicalOperatorType::LOGICAL_COPY_DATABASE;

public:
	explicit LogicalCopyDatabase(unique_ptr<CopyDatabaseInfo> info_p);
	~LogicalCopyDatabase() override;

	unique_ptr<CopyDatabaseInfo> info;

protected:
	void ResolveTypes() override;

private:
	explicit LogicalCopyDatabase(unique_ptr<ParseInfo> info_p);
};

} // namespace duckdb

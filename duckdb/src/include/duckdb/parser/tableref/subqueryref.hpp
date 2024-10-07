//===----------------------------------------------------------------------===//
//                         DuckDB
//
// duckdb/parser/tableref/subqueryref.hpp
//
//
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/parser/statement/select_statement.hpp"
#include "duckdb/parser/tableref.hpp"

namespace duckdb {
//! Represents a subquery
class SubqueryRef : public TableRef {
public:
	static constexpr const TableReferenceType TYPE = TableReferenceType::SUBQUERY;

	//! The subquery
	unique_ptr<SelectStatement> subquery;

public:
	string ToString() const override;
	bool Equals(const TableRef &other_p) const override;

	unique_ptr<TableRef> Copy() override;

	//! Deserializes a blob back into a SubqueryRef

	static unique_ptr<TableRef> Deserialize(Deserializer &source);
};
} // namespace duckdb
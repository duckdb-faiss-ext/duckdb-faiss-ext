#include "duckdb/main/prepared_statement.hpp"

#include "duckdb/common/exception.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/prepared_statement_data.hpp"

namespace duckdb {

PreparedStatement::PreparedStatement(shared_ptr<ClientContext> context, shared_ptr<PreparedStatementData> data_p,
                                     string query, case_insensitive_map_t<idx_t> named_param_map_p)
    : context(std::move(context)), data(std::move(data_p)), query(std::move(query)), success(true),
      named_param_map(std::move(named_param_map_p)) {
	D_ASSERT(data || !success);
}

PreparedStatement::PreparedStatement(ErrorData error) : context(nullptr), success(false), error(std::move(error)) {
}

PreparedStatement::~PreparedStatement() {
}

const string &PreparedStatement::GetError() {
	D_ASSERT(HasError());
	return error.Message();
}

ErrorData &PreparedStatement::GetErrorObject() {
	return error;
}

bool PreparedStatement::HasError() const {
	return !success;
}

idx_t PreparedStatement::ColumnCount() {
	D_ASSERT(data);
	return data->types.size();
}

StatementType PreparedStatement::GetStatementType() {
	D_ASSERT(data);
	return data->statement_type;
}

StatementProperties PreparedStatement::GetStatementProperties() {
	D_ASSERT(data);
	return data->properties;
}

const vector<LogicalType> &PreparedStatement::GetTypes() {
	D_ASSERT(data);
	return data->types;
}

const vector<string> &PreparedStatement::GetNames() {
	D_ASSERT(data);
	return data->names;
}

case_insensitive_map_t<LogicalType> PreparedStatement::GetExpectedParameterTypes() const {
	D_ASSERT(data);
	case_insensitive_map_t<LogicalType> expected_types(data->value_map.size());
	for (auto &it : data->value_map) {
		auto &identifier = it.first;
		D_ASSERT(data->value_map.count(identifier));
		D_ASSERT(it.second);
		expected_types[identifier] = it.second->GetValue().type();
	}
	return expected_types;
}

unique_ptr<QueryResult> PreparedStatement::Execute(case_insensitive_map_t<BoundParameterData> &named_values,
                                                   bool allow_stream_result) {
	return nullptr;
}

unique_ptr<QueryResult> PreparedStatement::Execute(vector<Value> &values, bool allow_stream_result) {
	return nullptr;
}

unique_ptr<PendingQueryResult> PreparedStatement::PendingQuery(vector<Value> &values, bool allow_stream_result) {
	return nullptr;
}

unique_ptr<PendingQueryResult> PreparedStatement::PendingQuery(case_insensitive_map_t<BoundParameterData> &named_values,
                                                               bool allow_stream_result) {
	return nullptr;
}

} // namespace duckdb

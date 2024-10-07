
#pragma once

#include "duckdb/common/optional_ptr.hpp"
#include "duckdb/common/vector_operations/binary_executor.hpp"
#include "duckdb/common/vector_operations/ternary_executor.hpp"
#include "duckdb/common/vector_operations/unary_executor.hpp"
#include "duckdb/common/vector_operations/vector_operations.hpp"
#include "duckdb/execution/expression_executor_state.hpp"
#include "duckdb/function/function.hpp"
#include "duckdb/storage/statistics/base_statistics.hpp"

namespace duckdb {

struct FunctionLocalState {
	DUCKDB_API virtual ~FunctionLocalState();

	template <class TARGET>
	TARGET &Cast() {
		DynamicCastCheck<TARGET>(this);
		return reinterpret_cast<TARGET &>(*this);
	}
	template <class TARGET>
	const TARGET &Cast() const {
		DynamicCastCheck<TARGET>(this);
		return reinterpret_cast<const TARGET &>(*this);
	}
};

struct ScalarFunctionInfo {
	DUCKDB_API virtual ~ScalarFunctionInfo();

	template <class TARGET>
	TARGET &Cast() {
		DynamicCastCheck<TARGET>(this);
		return reinterpret_cast<TARGET &>(*this);
	}
	template <class TARGET>
	const TARGET &Cast() const {
		DynamicCastCheck<TARGET>(this);
		return reinterpret_cast<const TARGET &>(*this);
	}
};

class Binder;
class BoundFunctionExpression;
class LogicalDependencyList;
class ScalarFunctionCatalogEntry;
struct StatementProperties;

struct FunctionStatisticsInput {
	FunctionStatisticsInput(BoundFunctionExpression &expr_p, optional_ptr<FunctionData> bind_data_p,
	                        vector<BaseStatistics> &child_stats_p, unique_ptr<Expression> *expr_ptr_p)
	    : expr(expr_p), bind_data(bind_data_p), expr_ptr(expr_ptr_p) {
	}

	BoundFunctionExpression &expr;
	optional_ptr<FunctionData> bind_data;
	unique_ptr<Expression> *expr_ptr;
};

struct FunctionModifiedDatabasesInput {
	FunctionModifiedDatabasesInput(optional_ptr<FunctionData> bind_data_p, StatementProperties &properties)
	    : bind_data(bind_data_p), properties(properties) {
	}

	optional_ptr<FunctionData> bind_data;
	StatementProperties &properties;
};

struct FunctionBindExpressionInput {
	FunctionBindExpressionInput(ClientContext &context_p, optional_ptr<FunctionData> bind_data_p,
	                            BoundFunctionExpression &function_p)
	    : context(context_p), bind_data(bind_data_p), function(function_p) {
	}

	ClientContext &context;
	optional_ptr<FunctionData> bind_data;
	BoundFunctionExpression &function;
};

//! The scalar function type
typedef std::function<void(DataChunk &, ExpressionState &, Vector &)> scalar_function_t;
//! The type to bind the scalar function and to create the function data
typedef unique_ptr<FunctionData> (*bind_scalar_function_t)(ClientContext &context, ScalarFunction &bound_function,
                                                           vector<unique_ptr<Expression>> &arguments);
//! The type to initialize a thread local state for the scalar function
typedef unique_ptr<FunctionLocalState> (*init_local_state_t)(ExpressionState &state,
                                                             const BoundFunctionExpression &expr,
                                                             FunctionData *bind_data);
//! The type to add the dependencies of this BoundFunctionExpression to the set of dependencies
typedef void (*dependency_function_t)(BoundFunctionExpression &expr, LogicalDependencyList &dependencies);
//! The type to propagate statistics for this scalar function
typedef unique_ptr<BaseStatistics> (*function_statistics_t)(ClientContext &context, FunctionStatisticsInput &input);
//! The type to bind lambda-specific parameter types
typedef LogicalType (*bind_lambda_function_t)(const idx_t parameter_idx, const LogicalType &list_child_type);
//! The type to bind lambda-specific parameter types
typedef void (*get_modified_databases_t)(ClientContext &context, FunctionModifiedDatabasesInput &input);

typedef void (*function_serialize_t)(Serializer &serializer, const optional_ptr<FunctionData> bind_data,
                                     const ScalarFunction &function);
typedef unique_ptr<FunctionData> (*function_deserialize_t)(Deserializer &deserializer, ScalarFunction &function);

//! The type to bind lambda-specific parameter types
typedef unique_ptr<Expression> (*function_bind_expression_t)(FunctionBindExpressionInput &input);

class ScalarFunction : public BaseScalarFunction { // NOLINT: work-around bug in clang-tidy
public:
	//! The main scalar function to execute
	scalar_function_t function;
	//! The bind function (if any)
	bind_scalar_function_t bind;
	//! Init thread local state for the function (if any)
	init_local_state_t init_local_state;
	//! The dependency function (if any)
	dependency_function_t dependency;
	//! The statistics propagation function (if any)
	function_statistics_t statistics;
	//! The lambda bind function (if any)
	bind_lambda_function_t bind_lambda;
	//! Function to bind the result function expression directly (if any)
	function_bind_expression_t bind_expression;
	//! Gets the modified databases (if any)
	get_modified_databases_t get_modified_databases;

	function_serialize_t serialize;
	function_deserialize_t deserialize;
	//! Additional function info, passed to the bind
	shared_ptr<ScalarFunctionInfo> function_info;

	DUCKDB_API bool operator==(const ScalarFunction &rhs) const;
	DUCKDB_API bool operator!=(const ScalarFunction &rhs) const;

	DUCKDB_API bool Equal(const ScalarFunction &rhs) const;

public:
	DUCKDB_API static void NopFunction(DataChunk &input, ExpressionState &state, Vector &result);

	template <class TA, class TR, class OP>
	static void UnaryFunction(DataChunk &input, ExpressionState &state, Vector &result) {
	}

	template <class TA, class TB, class TR, class OP>
	static void BinaryFunction(DataChunk &input, ExpressionState &state, Vector &result) {
	}

	template <class TA, class TB, class TC, class TR, class OP>
	static void TernaryFunction(DataChunk &input, ExpressionState &state, Vector &result) {
	}

public:
	template <class OP>
	static scalar_function_t GetScalarUnaryFunction(const LogicalType &type) {
		scalar_function_t function;
		switch (type.id()) {
		case LogicalTypeId::TINYINT:
			function = &ScalarFunction::UnaryFunction<int8_t, int8_t, OP>;
			break;
		case LogicalTypeId::SMALLINT:
			function = &ScalarFunction::UnaryFunction<int16_t, int16_t, OP>;
			break;
		case LogicalTypeId::INTEGER:
			function = &ScalarFunction::UnaryFunction<int32_t, int32_t, OP>;
			break;
		case LogicalTypeId::BIGINT:
			function = &ScalarFunction::UnaryFunction<int64_t, int64_t, OP>;
			break;
		case LogicalTypeId::UTINYINT:
			function = &ScalarFunction::UnaryFunction<uint8_t, uint8_t, OP>;
			break;
		case LogicalTypeId::USMALLINT:
			function = &ScalarFunction::UnaryFunction<uint16_t, uint16_t, OP>;
			break;
		case LogicalTypeId::UINTEGER:
			function = &ScalarFunction::UnaryFunction<uint32_t, uint32_t, OP>;
			break;
		case LogicalTypeId::UBIGINT:
			function = &ScalarFunction::UnaryFunction<uint64_t, uint64_t, OP>;
			break;
		case LogicalTypeId::HUGEINT:
			function = &ScalarFunction::UnaryFunction<hugeint_t, hugeint_t, OP>;
			break;
		case LogicalTypeId::UHUGEINT:
			function = &ScalarFunction::UnaryFunction<uhugeint_t, uhugeint_t, OP>;
			break;
		case LogicalTypeId::FLOAT:
			function = &ScalarFunction::UnaryFunction<float, float, OP>;
			break;
		case LogicalTypeId::DOUBLE:
			function = &ScalarFunction::UnaryFunction<double, double, OP>;
			break;
		default:
			throw InternalException("Unimplemented type for GetScalarUnaryFunction");
		}
		return function;
	}

	template <class TR, class OP>
	static scalar_function_t GetScalarUnaryFunctionFixedReturn(const LogicalType &type) {
		scalar_function_t function;
		switch (type.id()) {
		case LogicalTypeId::TINYINT:
			function = &ScalarFunction::UnaryFunction<int8_t, TR, OP>;
			break;
		case LogicalTypeId::SMALLINT:
			function = &ScalarFunction::UnaryFunction<int16_t, TR, OP>;
			break;
		case LogicalTypeId::INTEGER:
			function = &ScalarFunction::UnaryFunction<int32_t, TR, OP>;
			break;
		case LogicalTypeId::BIGINT:
			function = &ScalarFunction::UnaryFunction<int64_t, TR, OP>;
			break;
		case LogicalTypeId::UTINYINT:
			function = &ScalarFunction::UnaryFunction<uint8_t, TR, OP>;
			break;
		case LogicalTypeId::USMALLINT:
			function = &ScalarFunction::UnaryFunction<uint16_t, TR, OP>;
			break;
		case LogicalTypeId::UINTEGER:
			function = &ScalarFunction::UnaryFunction<uint32_t, TR, OP>;
			break;
		case LogicalTypeId::UBIGINT:
			function = &ScalarFunction::UnaryFunction<uint64_t, TR, OP>;
			break;
		case LogicalTypeId::HUGEINT:
			function = &ScalarFunction::UnaryFunction<hugeint_t, TR, OP>;
			break;
		case LogicalTypeId::UHUGEINT:
			function = &ScalarFunction::UnaryFunction<uhugeint_t, TR, OP>;
			break;
		case LogicalTypeId::FLOAT:
			function = &ScalarFunction::UnaryFunction<float, TR, OP>;
			break;
		case LogicalTypeId::DOUBLE:
			function = &ScalarFunction::UnaryFunction<double, TR, OP>;
			break;
		default:
			throw InternalException("Unimplemented type for GetScalarUnaryFunctionFixedReturn");
		}
		return function;
	}
};

} // namespace duckdb
#include "duckdb/function/function.hpp"
#include "duckdb/main/client_context.hpp"

namespace duckdb {
static unique_ptr<FunctionData> MoveToGPUBind(ClientContext &, TableFunctionBindInput &input,
                                              vector<LogicalType> &return_types, vector<string> &names);
static void MoveToGPUFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &);
} // namespace duckdb
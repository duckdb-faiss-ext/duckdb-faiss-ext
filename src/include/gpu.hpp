#include "duckdb/common/identifier.hpp"
#include "duckdb/function/function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/client_context.hpp"
#include "faiss/Index.h"

namespace duckdb {
unique_ptr<FunctionData> MoveToGPUBind(ClientContext &, TableFunctionBindInput &input,
                                       vector<LogicalType> &return_types, vector<Identifier> &names);
void MoveToGPUFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &);
vector<shared_ptr<faiss::SearchParameters>> innerCreateSearchParametersGPU(faiss::Index *index,
                                                                           faiss::IDSelector *selector,
                                                                           Vector *userParams, uint64_t paramCount,
                                                                           string prefix);
} // namespace duckdb
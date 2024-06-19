#include "duckdb/common/string.hpp"
#include "duckdb/common/types/vector.hpp"

#include <cstdint>

namespace duckdb {

// Vector must be a mapvector with paramCount values.
string getUserParamValue(Vector &userParams, uint64_t paramCount, string key);

// Value must be of type Map, which is just a list of structs (k-v pairs)
std::tuple<shared_ptr<Vector>, uint64_t> mapFromValue(Value v);

} // namespace duckdb

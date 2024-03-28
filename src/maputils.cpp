#include "duckdb/common/string.hpp"
#include "duckdb/common/types/vector.hpp"

#include <cstdint>

namespace duckdb {

// Vector must be a mapvector with paramCount values.
string getUserParamValue(Vector &userParams, uint64_t paramCount, string key) {
	if (paramCount == 0) {
		return "";
	}
	Vector &keys = MapVector::GetKeys(userParams);
	Vector &values = MapVector::GetValues(userParams);
	for (uint64_t i = 0; i < paramCount; i++) {
		if (keys.GetValue(i).GetValue<string>() == key) {
			return values.GetValue(i).GetValue<string>();
		}
	}
	return "";
}

// Value must be of type Map, which is just a list of structs (k-v pairs)
std::tuple<shared_ptr<Vector>, uint64_t> mapFromValue(Value v) {
	vector<Value> paramList = ListValue::GetChildren(v);
	Vector values = Vector(v);
	values.Reference(v);

	return std::make_tuple(make_shared<Vector>(v), paramList.size());
}
} // namespace duckdb

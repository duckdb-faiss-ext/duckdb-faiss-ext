#define DUCKDB_EXTENSION_MAIN

#include "faiss_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"

#include <faiss/index_factory.h>

#include <random>


#include <duckdb/parser/parsed_data/create_scalar_function_info.hpp>
#include "duckdb/parser/parsed_data/create_table_function_info.hpp"

namespace duckdb {

    struct IndexEntry {
        unique_ptr<faiss::Index> index;
        int dimension;
        vector<unique_ptr<float[]>> index_data;
    };


    static unordered_map<string, IndexEntry> indexes;

    struct CreateFunctionData : public TableFunctionData {
        CreateFunctionData() {
        }

        bool finished = false;
        string key = "";
        int dimension;
        string description;
    };

    static void CreateFunction(ClientContext &context, TableFunctionInput &data_p, DataChunk &output) {
        auto &data = (CreateFunctionData &) *data_p.bind_data;
        if (data.finished) {
            return;
        }

        IndexEntry entry;
        entry.dimension = data.dimension;
        entry.index = unique_ptr<faiss::Index>(faiss::index_factory(entry.dimension, data.description.c_str()));

        indexes[data.key] = std::move(entry);
        data.finished = true;
        output.SetCardinality(1);
        output.data[0].SetValue(0, Value::BOOLEAN(true));
    }


    static unique_ptr<FunctionData> CreateBind(ClientContext &context, TableFunctionBindInput &input,
                                               vector<LogicalType> &return_types, vector<string> &names) {
        auto result = make_unique<CreateFunctionData>();
        return_types.emplace_back(LogicalType::BOOLEAN);
        names.emplace_back("Success");

        result->key = input.inputs[0].ToString();
        result->dimension = input.inputs[1].GetValue<int>();
        result->description = input.inputs[2].ToString();

        return std::move(result);
        // TODO use Index Factory here
    }

    struct AddData : TableFunctionData {
        AddData(ClientContext &context) {
        }

        string key;
    };

    static unique_ptr<FunctionData> AddBind(ClientContext &context, TableFunctionBindInput &input,
                                            vector<LogicalType> &return_types, vector<string> &names) {

        if (input.input_table_types.size() != 1 || input.input_table_types[0].id() != LogicalTypeId::LIST) {
            throw InvalidInputException("Need table with single list column as input");
        }

        return_types.emplace_back(LogicalType::BOOLEAN);
        names.emplace_back("Success");

        auto result = make_unique<AddData>(context);
        result->key = input.inputs[0].ToString();


        if (indexes.find(result->key) == indexes.end()) {
            throw InvalidInputException("Could not find index %s", result->key);
        }


        return result;
    }

    static OperatorResultType AddFunction(ExecutionContext &context, TableFunctionInput &data_p, DataChunk &input,
                                          DataChunk &output) {

        auto bind_data = (AddData *) data_p.bind_data;
        D_ASSERT(bind_data);
        // FIXME

        D_ASSERT(indexes.find(bind_data->key) != indexes.end());
        auto &entry = indexes[bind_data->key];


        // TODO support adding with labels, first column of table ay

        auto &input_vector = input.data[0];
        input_vector.Flatten(input.size()); // FIXME use canonical
        D_ASSERT(input_vector.GetVectorType() == VectorType::FLAT_VECTOR);

        auto list_entries = ListVector::GetData(input_vector);

        for (idx_t row_idx = 0; row_idx < input.size(); row_idx++) {
            if (list_entries[row_idx].length != entry.dimension) {
                throw InvalidInputException("All list vectors need to have length %d, got %llu at index %llu",
                                            entry.dimension, list_entries[row_idx].length, row_idx);
            }
        }

        auto list_child = ListVector::GetEntry(input_vector);
        // TODO use canonical here as well

        auto data_elements = input.size() * entry.dimension;

        list_child.Flatten(data_elements);

        Vector cast_result(LogicalType::FLOAT, data_elements);
        VectorOperations::Cast(context.client, list_child, cast_result, data_elements, false);
        auto child_ptr = FlatVector::GetData<float>(cast_result);

        auto index_data = unique_ptr<float[]>(new float[data_elements]);
        memcpy(child_ptr, index_data.get(), data_elements * sizeof(float)); // TODO we should allocate this once, keep it around and then materialize the lists into it
        indexes[bind_data->key].index->add(input.size(), index_data.get());
        entry.index_data.push_back(std::move(index_data));

        return OperatorResultType::NEED_MORE_INPUT;
    }


    struct SearchData : TableFunctionData {
        SearchData(ClientContext &context) {
        }

        string key;
        int k;
    };

    static unique_ptr<FunctionData> SearchBind(ClientContext &context, TableFunctionBindInput &input,
                                               vector<LogicalType> &return_types, vector<string> &names) {

        if (input.input_table_types.size() != 1 || input.input_table_types[0].id() != LogicalTypeId::LIST) {
            throw InvalidInputException("Need table with single list column as input");
        }


        return_types.emplace_back(input.input_table_types[0]);
        child_list_t<LogicalType> struct_children;
        struct_children.emplace_back("rank", LogicalType::INTEGER);
        struct_children.emplace_back("label", LogicalType::BIGINT);
        struct_children.emplace_back("distance", LogicalType::FLOAT);

        return_types.emplace_back(LogicalType::LIST(LogicalType::STRUCT(std::move(struct_children))));

        names.emplace_back("input");
        names.emplace_back("results");

        auto result = make_unique<SearchData>(context);
        result->key = input.inputs[0].ToString();
        result->k = input.inputs[1].GetValue<int>();

        if (indexes.find(result->key) == indexes.end()) {
            throw InvalidInputException("Could not find index %s", result->key);
        }

        return result;
    }

    static OperatorResultType SearchFunction(ExecutionContext &context, TableFunctionInput &data_p, DataChunk &input,
                                             DataChunk &output) {

        auto bind_data = (SearchData *) data_p.bind_data;
        D_ASSERT(bind_data);

        // TODO not an assertion this can totally happen
        D_ASSERT(indexes.find(bind_data->key) != indexes.end());
        auto &entry = indexes[bind_data->key];

        auto &input_vector = input.data[0];
        input_vector.Flatten(input.size()); // FIXME use canonical
        D_ASSERT(input_vector.GetVectorType() == VectorType::FLAT_VECTOR);

        auto list_entries = ListVector::GetData(input_vector);

        for (idx_t row_idx = 0; row_idx < input.size(); row_idx++) {
            if (list_entries[row_idx].length != entry.dimension) {
                throw InvalidInputException("All list vectors need to have length %d, got %llu at index %llu",
                                            entry.dimension, list_entries[row_idx].length, row_idx);
            }
        }

        auto list_child = ListVector::GetEntry(input_vector);
        // TODO use canonical vector representation here as well
        list_child.Flatten(input.size() * entry.dimension);

        Vector cast_result(LogicalType::FLOAT, input.size() * entry.dimension);
        VectorOperations::Cast(context.client, list_child, cast_result, input.size() * entry.dimension, false);
        auto child_ptr = FlatVector::GetData<float>(cast_result);

        auto n_queries = input.size();

        auto labels = unique_ptr<faiss::idx_t[]>(new faiss::idx_t[n_queries * bind_data->k]);
        auto distances = unique_ptr<float[]>(new float[n_queries * bind_data->k]);

        // the actual search woo
        indexes[bind_data->key].index->search(n_queries, child_ptr, bind_data->k, distances.get(), labels.get());

        output.data[0].Reference(input.data[0]);


        auto& result_list_vector = output.data[1];
        ListVector::SetListSize(result_list_vector, n_queries * bind_data->k);
        auto list_ptr = ListVector::GetData(result_list_vector);
        auto& result_struct_vector = ListVector::GetEntry(result_list_vector);
        auto& struct_entries = StructVector::GetEntries(result_struct_vector);
        auto rank_ptr = FlatVector::GetData<int32_t>(*struct_entries[0]);
        auto label_ptr = FlatVector::GetData<int64_t>(*struct_entries[1]);
        auto distance_ptr = FlatVector::GetData<float>(*struct_entries[2]);

        idx_t list_offset = 0;

        for (idx_t row_idx = 0; row_idx < n_queries; row_idx++) {
            list_ptr[row_idx].length = bind_data->k;
            list_ptr[row_idx].offset = list_offset;

            for (idx_t res_idx = 0; res_idx < bind_data->k; res_idx++) {
                rank_ptr[list_offset + res_idx] = res_idx;
                label_ptr[list_offset + res_idx] = labels[row_idx * bind_data->k + res_idx];
                distance_ptr[list_offset + res_idx] = distances[row_idx * bind_data->k + res_idx];
            }
            list_offset += bind_data->k;
        }

        output.SetCardinality(n_queries);

        return OperatorResultType::NEED_MORE_INPUT;
    }


    static void LoadInternal(DatabaseInstance &instance) {
        Connection con(instance);
        con.BeginTransaction();
        auto &catalog = Catalog::GetSystemCatalog(*con.context);

        {
            TableFunction create_func("faiss_create", {LogicalType::VARCHAR, LogicalType::INTEGER, LogicalType::VARCHAR}, CreateFunction,
                                      CreateBind);
            CreateTableFunctionInfo create_info(create_func);
            catalog.CreateTableFunction(*con.context, &create_info);
        }

        {
            TableFunction add_function("faiss_add", {LogicalType::VARCHAR, LogicalType::TABLE}, nullptr, AddBind);
            add_function.in_out_function = AddFunction;
            CreateTableFunctionInfo add_info(add_function);
            catalog.CreateTableFunction(*con.context, &add_info);
        }

        {
            TableFunction search_function("faiss_search",
                                          {LogicalType::VARCHAR, LogicalType::INTEGER, LogicalType::TABLE}, nullptr,
                                          SearchBind);
            search_function.in_out_function = SearchFunction;
            CreateTableFunctionInfo search_info(search_function);
            catalog.CreateTableFunction(*con.context, &search_info);
        }


        con.Commit();

    }

    void FaissExtension::Load(DuckDB &db) {
        LoadInternal(*db.instance);
    }

    std::string FaissExtension::Name() {
        return "faiss";
    }

} // namespace duckdb

extern "C" {

DUCKDB_EXTENSION_API void faiss_init(duckdb::DatabaseInstance &db) {
    LoadInternal(db);
}

DUCKDB_EXTENSION_API const char *faiss_version() {
    return duckdb::DuckDB::LibraryVersion();
}
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif

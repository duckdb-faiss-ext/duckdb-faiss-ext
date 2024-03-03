package main

import (
	"database/sql"
	"fmt"
	"os"

	_ "github.com/marcboeker/go-duckdb"
)

func main() {
	db, err := sql.Open("duckdb", "?allow_unsigned_extensions=true")
	if err != nil {
		panic(err)
	}
	row := db.QueryRow("SELECT version()")
	if err != nil {
		panic(err)
	}
	var version string
	err = row.Scan(&version)
	if err != nil {
		panic(err)
	}
	fmt.Println(version)
	loadFaiss(db)

	fmt.Println("whoo")

	_, err = db.Exec("CALL FAISS_LOAD('flat', '../conformanceTests/index_IVF2048')")
	if err != nil {
		panic(err)
	}

	_, err = db.Exec("CREATE TABLE queries AS SELECT qid, vector AS embedding FROM '../conformanceTests/anserini-tools/topics-and-qrels/topics.dl19-passage.openai-ada2.jsonl.gz'")
	if err != nil {
		panic(err)
	}
	rows, err := db.Query("SELECT qid, UNNEST(faiss_search('flat', 1000, embedding)) FROM queries")
	if err != nil {
		panic(err)
	}
	types, err := rows.ColumnTypes()
	if err != nil {
		panic(err)
	}
	fmt.Println(types[0].ScanType(), types[0].DatabaseTypeName())

	file, err := os.Create("results")
	if err != nil {
		panic(err)
	}
	var id int
	var x map[string]interface{}
	for rows.Next() {
		rows.Scan(&id, &x)
		fmt.Println(id, x, x["label"])
		fmt.Printf("%T\n", x["label"])
		if x["label"].(int64) != -1 {
			fmt.Fprintln(file, id, 0, x["label"], x["distance"], x["rank"], "TODO")
		}
	}
}

func loadFaiss(db *sql.DB) {
	_, err := db.Exec("LOAD '../build/reldebug/extension/faiss/faiss.duckdb_extension';")
	if err != nil {
		panic(err)
	}
}

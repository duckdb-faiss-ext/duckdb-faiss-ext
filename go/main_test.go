package main

import (
	"database/sql"
	"fmt"
	"math"
	"strconv"
	"strings"
	"testing"

	_ "github.com/ianlancetaylor/cgosymbolizer"
)

const targetPass = 0.99

// func BenchmarkAllQueriesIVF2048_10_all(b *testing.B) {
// 	b.StopTimer()
// 	db, err := sql.Open("duckdb", "?allow_unsigned_extensions=true")
// 	if err != nil {
// 		panic(err)
// 	}
// 	loadFaiss(db)

// 	_, err = db.Exec("CALL FAISS_LOAD('flat', '../conformanceTests/index_IVF2048')")
// 	if err != nil {
// 		b.Logf("failed loading faiss index: %v", err)
// 		b.FailNow()
// 	}

// 	_, err = db.Exec("CREATE TABLE queries AS SELECT qid, vector AS embedding FROM '../conformanceTests/anserini-tools/topics-and-qrels/topics.dl19-passage.openai-ada2.jsonl.gz'")
// 	if err != nil {
// 		b.Logf("loading query vectors: %v", err)
// 		b.FailNow()
// 	}
// 	b.StartTimer()
// 	for n := 0; n < b.N; n++ {
// 		rows, err := db.Query("SELECT qid, UNNEST(faiss_search('flat', 10, embedding)) FROM queries")
// 		if err != nil {
// 			b.Logf("executing query: %v", err)
// 			b.FailNow()
// 		}

// 		var id int
// 		var x map[string]interface{}
// 		for rows.Next() {
// 			rows.Scan(&id, &x)
// 		}
// 		if rows.Err() != nil {
// 			b.Logf("Error while fetching rows: %v", rows.Err())
// 			b.FailNow()
// 		}
// 	}
// }

func BenchmarkAllQueriesIVF2048_10(b *testing.B) {
	const requiredResults = 10

	db, err := sql.Open("duckdb", "?allow_unsigned_extensions=true")
	if err != nil {
		panic(err)
	}
	createSequentialTable(b, db, "ids", "id", 1000000)
	loadFaiss(db)

	_, err = db.Exec("CALL FAISS_LOAD('flat', '../conformanceTests/index_IVF2048')")
	if err != nil {
		b.Logf("failed loading faiss index: %v", err)
		b.FailNow()
	}

	_, err = db.Exec("CREATE TABLE queries AS SELECT qid, vector AS embedding FROM '../conformanceTests/anserini-tools/topics-and-qrels/topics.dl19-passage.openai-ada2.jsonl.gz'")
	if err != nil {
		b.Logf("loading query vectors: %v", err)
		b.FailNow()
	}
	for p := 1; p < 100; p++ {
		p := p
		// binom := distuv.Binomial{
		// 	P: float64(p) / 100,
		// }
		// // requiredN, _ := bisectRootN(func(x float64) float64 {
		// 	binom.N = x
		// 	return binom.CDF(requiredResults) - targetPass
		// }, 0, 100000)

		// b.Run(fmt.Sprintf("%02d%%_nonfilter", p), func(b *testing.B) {
		// 	for n := 0; n < b.N; n++ {
		// 		// This query has been verified to actuall return values with correct label
		// 		query := fmt.Sprintf("SELECT qid, UNNEST(faiss_search('flat', %d, embedding)) FROM queries", requiredN)
		// 		rows, err := db.Query(query)
		// 		if err != nil {
		// 			b.Logf("executing query: %v", err)
		// 			b.Logf("related query:%v", query)
		// 			b.FailNow()
		// 		}

		// 		var id int
		// 		var x map[string]interface{}
		// 		for rows.Next() {
		// 			rows.Scan(&id, &x)
		// 		}
		// 		if rows.Err() != nil {
		// 			b.Logf("Error while fetching rows: %v", rows.Err())
		// 			b.FailNow()
		// 		}
		// 	}
		// })
		b.Run(fmt.Sprintf("%02d%%_filter", p), func(b *testing.B) {
			for n := 0; n < b.N; n++ {
				// This query has been verified to actuall return values with correct label
				query := fmt.Sprintf("SELECT qid, UNNEST(faiss_search_filter('flat', %d, embedding, 'id%%100<%d', 'id', 'ids')) FROM queries", requiredResults, p)
				rows, err := db.Query(query)
				if err != nil {
					b.Logf("executing query: %v", err)
					b.Logf("related query:%v", query)
					b.FailNow()
				}

				var id int
				var x map[string]interface{}
				if rows.Err() != nil {
					b.Logf("Error while fetching rows: %v", rows.Err())
					b.FailNow()
				}
				for rows.Next() {
					rows.Scan(&id, &x)
				}
			}
		})
	}
}

// func BenchmarkModuloOperator(b *testing.B) {
// 	db, err := sql.Open("duckdb", "?allow_unsigned_extensions=true")
// 	if err != nil {
// 		panic(err)
// 	}
// 	createSequentialTable(b, db, "ids", "id", 1000000)
// 	for n := 0; n < b.N; n++ {
// 		_, err := db.Exec("SELECT id//100 from ids")
// 		if err != nil {
// 			b.Log(err)
// 			b.FailNow()
// 		}
// 	}
// }

func createSequentialTable(b *testing.B, db *sql.DB, tablename, columnname string, n int) {
	_, err := db.Exec("CREATE TABLE " + tablename + " (" + columnname + " UINTEGER NOT NULL)")
	if err != nil {
		b.Logf("error creating prepared statement: %v", err)
		b.FailNow()
	}

	var command strings.Builder
	command.WriteString("INSERT INTO " + tablename + " VALUES")
	for i := 0; i < n; i++ {
		command.WriteString("(" + strconv.Itoa(i) + "),")
	}
	_, err = db.Exec(command.String())
	if err != nil {
		b.Logf("error creating sequential table: %v", err)
		b.FailNow()
	}
}

func bisectRootN(f func(float64) float64, min, max int64) (int64, error) {
	l := f(float64(min))
	r := f(float64(max))
	if max == min+1 {
		if math.Abs(l) < math.Abs(r) {
			return min, nil
		} else {
			return max, nil
		}
	}

	m := f(float64((min + max) / 2))
	if m == 0.0 {
		if 0 < r {
			return bisectRootN(f, (min+max)/2, max)
		} else {
			return bisectRootN(f, min, (min+max)/2)
		}
	} else if math.Signbit(m) == math.Signbit(r) {
		return bisectRootN(f, min, (min+max)/2)
	} else if math.Signbit(m) == math.Signbit(l) {
		return bisectRootN(f, (min+max)/2, max)
	}
	return 0, fmt.Errorf("input should be of opposite sign!")
}

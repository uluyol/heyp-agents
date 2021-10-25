package montecarlo

import (
	"fmt"
	"reflect"
)

// populateSummary populates summaryOut (preserving the values of untouched fields)
// with Stats of metrics in resultStruct. It returns summaryOut.
//
// Sample usage:
//
//    type result struct {
//        M1 metric
//        M2 metricWithAbsVal
//    }
//
//    type summary struct {
//        X     float64 // preserved
//        M1    Stats   // Set to M1.Stats()
//        M2    Stats   // Set to M2.Raw.Stats()
//        AbsM2 Stats   // Set to M2.Abs.Stats()
//    }
//
//    var x = populateSummary(&summary{...}, &result{...}).(*summary)
//
func populateSummary(summaryOut interface{}, resultStruct interface{}) interface{} {
	resultType := reflect.TypeOf(resultStruct)

	if resultType.Kind() != reflect.Ptr {
		panic(fmt.Errorf("resultStruct must be a pointer to a struct: is a %v", resultType.Kind()))
	}
	resultElem := resultType.Elem()
	if resultElem.Kind() != reflect.Struct {
		panic(fmt.Errorf("resultStruct must be a pointer to a struct: is a pointer to %v", resultElem.Kind()))
	}

	summaryType := reflect.TypeOf(summaryOut)
	if summaryType.Kind() != reflect.Ptr {
		panic(fmt.Errorf("summaryOut must be a pointer to a struct: is a %v", summaryType.Kind()))
	}
	if summaryType.Elem().Kind() != reflect.Struct {
		panic(fmt.Errorf("summaryOut must be a pointer to a struct: is a pointer to %v", summaryType.Elem().Kind()))
	}

	resultVal := reflect.ValueOf(resultStruct).Elem()
	resultFields := resultVal.NumField()
	summary := reflect.ValueOf(summaryOut).Elem()
	for fieldID := 0; fieldID < resultFields; fieldID++ {
		fieldType := resultElem.Field(fieldID)
		if !fieldType.IsExported() {
			continue
		}

		field := resultVal.Field(fieldID)

		metricData, isMetric := field.Interface().(metric)
		metricWithAbsValData, isMetricWithAbsVal := field.Interface().(metricWithAbsVal)

		if isMetric {
			field := summary.FieldByName(fieldType.Name)
			if !field.IsValid() {
				panic(fmt.Errorf("%T has no field named %q", summaryOut, fieldType.Name))
			}
			field.Set(reflect.ValueOf(metricData.Stats()))
		} else if isMetricWithAbsVal {
			rawField := summary.FieldByName(fieldType.Name)
			if !field.IsValid() {
				panic(fmt.Errorf("%T has no field named %q", summaryOut, fieldType.Name))
			}
			rawField.Set(reflect.ValueOf(metricWithAbsValData.Raw.Stats()))

			absField := summary.FieldByName("Abs" + fieldType.Name)
			if !field.IsValid() {
				panic(fmt.Errorf("%T has no field named %q", summaryOut, "Abs"+fieldType.Name))
			}
			absField.Set(reflect.ValueOf(metricWithAbsValData.Abs.Stats()))
		}
	}
	return summaryOut
}

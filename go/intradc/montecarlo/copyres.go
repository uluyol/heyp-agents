package montecarlo

import (
	"errors"
	"fmt"
	"reflect"
)

// mergeMetricsInto takes two structs (of the same type) and merges the data in src
// to dst.
//
// Merging the data is done as follows:
// - For fields that are of type metric or metricWithAbsVal, it calls dst.F.MergeFrom(&src.F)
// - For fields that are of type float64, it does dst.F += src.F
func mergeMetricsInto(src, dst interface{}) {
	typ := reflect.TypeOf(src)
	typ2 := reflect.TypeOf(dst)
	if typ != typ2 {
		panic(fmt.Errorf("src type %v != dst type %v", typ, typ2))
	}

	if typ.Kind() != reflect.Ptr {
		panic(errors.New("data must be a pointer value"))
	}

	stType := typ.Elem()
	if stType.Kind() != reflect.Struct {
		panic(fmt.Errorf("data must be a pointer to struct, got pointer to %v", stType))
	}

	srcVal := reflect.ValueOf(src).Elem()
	dstVal := reflect.ValueOf(dst).Elem()

	numFields := stType.NumField()
	for fieldID := 0; fieldID < numFields; fieldID++ {
		fieldType := stType.Field(fieldID)
		if !fieldType.IsExported() {
			continue
		}

		srcIface := srcVal.Field(fieldID).Addr().Interface()
		dstIface := dstVal.Field(fieldID).Addr().Interface()

		switch srcP := srcIface.(type) {
		case *metric:
			dstP := dstIface.(*metric)
			dstP.MergeFrom(srcP)
		case *metricWithAbsVal:
			dstP := dstIface.(*metricWithAbsVal)
			dstP.MergeFrom(srcP)
		case *float64:
			dstP := dstIface.(*float64)
			*dstP += *srcP
		case *int:
			dstP := dstIface.(*int)
			*dstP += *srcP
		default:
			panic(fmt.Errorf("unknown type %T", srcIface))
		}
	}
}

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

		// Format of this tag = dist:"collect"
		// If the value is empty or unspecified, a dist will not be collected
		distTagVal := fieldType.Tag.Get("dist")
		collectDist := false
		if distTagVal != "" {
			if distTagVal == "collect" {
				collectDist = true
			} else {
				panic(fmt.Errorf("field %s has invalid dist tag: wanted \"collect\", value was %q", fieldType.Name, distTagVal))
			}
		}

		field := resultVal.Field(fieldID)

		metricData, isMetric := field.Interface().(metric)
		metricWithAbsValData, isMetricWithAbsVal := field.Interface().(metricWithAbsVal)

		if isMetric {
			field := summary.FieldByName(fieldType.Name)
			if !field.IsValid() {
				panic(fmt.Errorf("%T has no field named %q", summaryOut, fieldType.Name))
			}
			field.Set(reflect.ValueOf(metricData.Stats(collectDist)))
		} else if isMetricWithAbsVal {
			rawField := summary.FieldByName(fieldType.Name)
			if !field.IsValid() {
				panic(fmt.Errorf("%T has no field named %q", summaryOut, fieldType.Name))
			}
			rawField.Set(reflect.ValueOf(metricWithAbsValData.Raw.Stats(collectDist)))

			absField := summary.FieldByName("Abs" + fieldType.Name)
			if !field.IsValid() {
				panic(fmt.Errorf("%T has no field named %q", summaryOut, "Abs"+fieldType.Name))
			}
			absField.Set(reflect.ValueOf(metricWithAbsValData.Abs.Stats(collectDist)))
		}
	}
	return summaryOut
}

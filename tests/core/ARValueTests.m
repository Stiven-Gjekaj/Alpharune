// Tests of the values of GML and of the collector.

#import "ARTest.h"
#include "ar_value.h"

AR_TEST(test_value_formats_numbers_as_string_does) {
    char text[32];
    ar_format_real(5, text, sizeof(text));
    AR_EQUAL(@(text), @"5");
    ar_format_real(-12, text, sizeof(text));
    AR_EQUAL(@(text), @"-12");
    ar_format_real(0.5, text, sizeof(text));
    AR_EQUAL(@(text), @"0.50");
    ar_format_real(1.0 / 3, text, sizeof(text));
    AR_EQUAL(@(text), @"0.33");
    ar_format_real(-0.0, text, sizeof(text));
    AR_EQUAL(@(text), @"0");
}

AR_TEST(test_value_compares_numbers_with_an_epsilon) {
    AR_EXPECT(ar_equal(ar_real(0.1 + 0.2), ar_real(0.3)));
    AR_EXPECT(ar_equal(ar_real(1), ar_real(1.000009)));
    AR_EXPECT(!ar_equal(ar_real(1), ar_real(1.0001)));
    int ordered;
    AR_EQUAL_INT(ar_compare(ar_real(2), ar_real(3), &ordered), -1);
    AR_EXPECT(ordered);
    AR_EQUAL_INT(ar_compare(ar_int64(5), ar_real(5), &ordered), 0);
    AR_EXPECT(ar_equal(ar_undefined(), ar_undefined()));
    AR_EXPECT(!ar_equal(ar_undefined(), ar_real(0)));
}

AR_TEST(test_value_compares_strings_and_refuses_a_number_with_a_string) {
    ARHeap *heap = ar_heap_new();
    ARValue a = ar_strv(ar_str_cstr(heap, "apple"));
    ARValue b = ar_strv(ar_str_cstr(heap, "apple"));
    ARValue c = ar_strv(ar_str_cstr(heap, "banana"));
    AR_EXPECT(ar_equal(a, b));
    int ordered;
    AR_EQUAL_INT(ar_compare(a, c, &ordered), -1);
    AR_EXPECT(!ar_equal(ar_strv(ar_str_cstr(heap, "1")), ar_real(1)));
    ar_compare(ar_real(1), a, &ordered);
    AR_EXPECT(!ordered);
    ar_heap_free(heap);
}

AR_TEST(test_value_truth_is_above_one_half) {
    AR_EXPECT(!ar_truthy(ar_real(0.5)));
    AR_EXPECT(ar_truthy(ar_real(0.51)));
    AR_EXPECT(!ar_truthy(ar_real(-1)));
    AR_EXPECT(!ar_truthy(ar_undefined()));
    AR_EXPECT(ar_truthy(ar_bool(1)));
}

AR_TEST(test_value_converts_to_integers_toward_zero) {
    AR_EQUAL_INT(ar_to_int(ar_real(2.9)), 2);
    AR_EQUAL_INT(ar_to_int(ar_real(-2.9)), -2);
    AR_EQUAL_INT(ar_to_int(ar_real(1e20)), INT32_MAX);
    AR_EQUAL_INT(ar_to_int(ar_real(0.0 / 0.0)), 0);
}

AR_TEST(test_value_counts_utf8_characters) {
    ARHeap *heap = ar_heap_new();
    ARStr *s = ar_str_cstr(heap, "a\xc3\xa9z\xe2\x98\x85!");  // a, e acute, z, a star, !
    AR_EQUAL_INT(s->length, 8);
    AR_EQUAL_INT(ar_str_chars(s), 5);
    AR_EQUAL_INT(ar_str_offset(s, 2), 3);
    AR_EQUAL_INT(ar_str_offset(s, 4), 7);
    AR_EQUAL_INT(ar_str_offset(s, 9), 8);
    ARStr *t = ar_str_concat(heap, s, ar_str_cstr(heap, "ok"));
    AR_EQUAL(@(t->text), @"aéz★!ok");
    AR_EQUAL_INT(ar_str_chars(t), 7);
    ar_heap_free(heap);
}

AR_TEST(test_value_grows_an_array_with_zeros) {
    ARHeap *heap = ar_heap_new();
    ARArray *a = ar_array_new(heap, 0);
    AR_EXPECT(ar_array_set(a, 5, ar_real(7)));
    AR_EQUAL_INT(a->length, 6);
    AR_EQUAL_INT(ar_array_get(a, 2).type, AR_REAL);
    AR_EQUAL_INT(ar_array_get(a, 5).r, 7);
    AR_EQUAL_INT(ar_array_get(a, 6).type, AR_UNDEFINED);
    AR_EXPECT(!ar_array_set(a, -1, ar_real(1)));
    ar_heap_free(heap);
}

AR_TEST(test_value_map_keeps_many_keys_and_removes_them) {
    ARVarMap map = {0};
    for (int32_t k = 0; k < 1000; k++) ar_vars_slot(&map, k * 7)->type = AR_REAL, ar_vars_slot(&map, k * 7)->r = k;
    AR_EQUAL_INT(map.count, 1000);
    for (int32_t k = 0; k < 1000; k += 2) AR_EXPECT(ar_vars_remove(&map, k * 7));
    AR_EQUAL_INT(map.count, 500);
    int bad = 0;
    for (int32_t k = 0; k < 1000; k++) {
        ARValue *v = ar_vars_find(&map, k * 7);
        if ((k % 2 == 0) != (v == NULL)) bad++;
        if (v && v->r != k) bad++;
    }
    AR_EQUAL_INT(bad, 0);
    ar_vars_clear(&map);
}

AR_TEST(test_value_collector_keeps_what_a_root_reaches) {
    ARHeap *heap = ar_heap_new();
    ARStruct *root = ar_struct_new(heap);
    ARArray *kept = ar_array_new(heap, 0);
    ar_array_set(kept, 0, ar_strv(ar_str_cstr(heap, "kept")));
    *ar_vars_slot(&root->vars, 1) = ar_arrayv(kept);
    ARMethod *m = ar_method_new(heap, 3, ar_structv(root));
    *ar_vars_slot(&root->vars, 2) = (ARValue){.type = AR_METHOD, .m = m};
    for (int i = 0; i < 100; i++) ar_str_cstr(heap, "garbage");
    ar_array_new(heap, 10);
    size_t before = ar_heap_bytes(heap);
    ar_mark_obj(&root->h);
    ar_heap_sweep(heap);
    AR_EXPECT(ar_heap_bytes(heap) < before);
    AR_EQUAL(@(ar_array_get(kept, 0).s->text), @"kept");
    // A second sweep with no marks frees all.
    ar_heap_sweep(heap);
    AR_EQUAL_INT(ar_heap_bytes(heap), 0);
    ar_heap_free(heap);
}

AR_TEST(test_value_collector_keeps_a_fixed_string) {
    ARHeap *heap = ar_heap_new();
    ARStr *s = ar_str_cstr(heap, "constant");
    s->h.fixed = 1;
    ar_heap_sweep(heap);
    AR_EQUAL(@(s->text), @"constant");
    ar_heap_free(heap);
}

AR_TEST(test_value_writes_the_text_of_each_type) {
    ARHeap *heap = ar_heap_new();
    ARArray *a = ar_array_new(heap, 0);
    ar_array_set(a, 0, ar_real(1));
    ar_array_set(a, 1, ar_strv(ar_str_cstr(heap, "x")));
    AR_EQUAL(@(ar_to_str(heap, ar_arrayv(a))->text), @"[ 1,\"x\" ]");
    AR_EQUAL(@(ar_to_str(heap, ar_undefined())->text), @"undefined");
    AR_EQUAL(@(ar_to_str(heap, ar_int64(-9000000000LL))->text), @"-9000000000");
    ar_heap_free(heap);
}

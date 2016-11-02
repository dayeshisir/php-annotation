#include "syaokun219.h"

static zend_function_entry syaokun219_function_entry[] = {
    ZEND_FE(syaokun219, NULL)
    {NULL, NULL, NULL}
};

ZEND_FUNCTION(syaokun219);

zend_module_entry syaokun219_module_entry = {
#if ZEND_MODULE_API_NO >= 20010901
    STANDARD_MODULE_HEADER,
#endif
    "syaokun219", //这个地方是扩展名称，往往我们会在这个地方使用一个宏。
    syaokun219_function_entry, /* Functions */
    NULL, /* MINIT */
    NULL, /* MSHUTDOWN */
    NULL, /* RINIT */
    NULL, /* RSHUTDOWN */
    NULL, /* MINFO */
#if ZEND_MODULE_API_NO >= 20010901
    "2.1", //这个地方是我们扩展的版本
#endif
    STANDARD_MODULE_PROPERTIES
};

ZEND_FUNCTION(syaokun219) {
    zval* foo;
    if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "z", foo) == FAILURE) {
        php_printf("empty input");
        RETURN_NULL();
    }
    switch(Z_TYPE_P(foo)) {
        case IS_ARRAY:
            php_printf("array");
            break;
        default:
            php_printf("not a array");
    }
    RETURN_STRING("My Name is Yaokun", 1);
}

#ifdef COMPILE_DL_SYAOKUN219
ZEND_GET_MODULE(syaokun219)
#endif

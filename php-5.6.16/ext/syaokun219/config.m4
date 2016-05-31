PHP_ARG_ENABLE(syaokun219,
    [Whether to enable the "syaokun219" extension],
    [  enable-syaokun219        Enable "syaokun219" extension support])

if test $PHP_SYAOKUN219 != "no"; then
    PHP_SUBST(SYAOKUN219_SHARED_LIBADD)
    PHP_NEW_EXTENSION(syaokun219, syaokun219.c, $ext_shared)
fi

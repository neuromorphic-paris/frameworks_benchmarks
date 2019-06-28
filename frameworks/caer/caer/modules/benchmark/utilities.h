#define BENCHMARK_WRAP_CONSTRUCT_0(class)\
class* class ## _construct() {\
    try {\
        return new class();\
    } catch (const std::exception& exception) {\
        caerLog(CAER_LOG_ERROR, #class "_construct", "failed with C++ exception: %s", exception.what());\
    }\
    return nullptr;\
}

#define BENCHMARK_WRAP_CONSTRUCT_1(class, argument_type_0)\
class* class ## _construct(argument_type_0 argument_0) {\
    try {\
        return new class(argument_0);\
    } catch (const std::exception& exception) {\
        caerLog(CAER_LOG_ERROR, #class "_construct", "failed with C++ exception: %s", exception.what());\
    }\
    return nullptr;\
}

#define BENCHMARK_WRAP_CONSTRUCT_2(class, argument_type_0, argument_type_1)\
class* class ## _construct(argument_type_0 argument_0, argument_type_1 argument_1) {\
    try {\
        return new class(argument_0, argument_1);\
    } catch (const std::exception& exception) {\
        caerLog(CAER_LOG_ERROR, #class "_construct", "failed with C++ exception: %s", exception.what());\
    }\
    return nullptr;\
}

#define BENCHMARK_WRAP_CONSTRUCT_3(class, argument_type_0, argument_type_1, argument_type_2)\
class* class ## _construct(argument_type_0 argument_0, argument_type_1 argument_1, argument_type_2 argument_2) {\
    try {\
        return new class(argument_0, argument_1, argument_2);\
    } catch (const std::exception& exception) {\
        caerLog(CAER_LOG_ERROR, #class "_construct", "failed with C++ exception: %s", exception.what());\
    }\
    return nullptr;\
}

#define BENCHMARK_WRAP_CONSTRUCT_4(class, argument_type_0, argument_type_1, argument_type_2, argument_type_3)\
class* class ## _construct(argument_type_0 argument_0, argument_type_1 argument_1, argument_type_2 argument_2, argument_type_3 argument_3) {\
    try {\
        return new class(argument_0, argument_1, argument_2, argument_3);\
    } catch (const std::exception& exception) {\
        caerLog(CAER_LOG_ERROR, #class "_construct", "failed with C++ exception: %s", exception.what());\
    }\
    return nullptr;\
}

#define BENCHMARK_WRAP_CONSTRUCT_5(class, argument_type_0, argument_type_1, argument_type_2, argument_type_3, argument_type_4)\
class* class ## _construct(argument_type_0 argument_0, argument_type_1 argument_1, argument_type_2 argument_2, argument_type_3 argument_3, argument_type_4 argument_4) {\
    try {\
        return new class(argument_0, argument_1, argument_2, argument_3, argument_4);\
    } catch (const std::exception& exception) {\
        caerLog(CAER_LOG_ERROR, #class "_construct", "failed with C++ exception: %s", exception.what());\
    }\
    return nullptr;\
}

#define BENCHMARK_WRAP_CONSTRUCT_5(class, argument_type_0, argument_type_1, argument_type_2, argument_type_3, argument_type_4)\
class* class ## _construct(argument_type_0 argument_0, argument_type_1 argument_1, argument_type_2 argument_2, argument_type_3 argument_3, argument_type_4 argument_4) {\
    try {\
        return new class(argument_0, argument_1, argument_2, argument_3, argument_4);\
    } catch (const std::exception& exception) {\
        caerLog(CAER_LOG_ERROR, #class "_construct", "failed with C++ exception: %s", exception.what());\
    }\
    return nullptr;\
}

#define BENCHMARK_WRAP_DESTRUCT(class)\
void class ## _destruct(class* class ## _instance) {\
    try {\
        delete class ## _instance;\
    } catch (const std::exception& exception) {\
        caerLog(CAER_LOG_ERROR, #class "_destruct", "failed with C++ exception: %s", exception.what());\
    }\
}

#define BENCHMARK_WRAP(class, return_type, function, default)\
return_type class ## _ ## function(class* class ## _instance) {\
    try {\
        return class ## _instance->function();\
    } catch (const std::exception& exception) {\
        caerLog(CAER_LOG_ERROR, #class "_" #function, "failed with C++ exception: %s", exception.what());\
    }\
    return default;\
}

#define BENCHMARK_WRAP_VOID_1(class, function, argument_type_0)\
void class ## _ ## function(class* class ## _instance, argument_type_0 argument_0) {\
    try {\
        class ## _instance->function(argument_0);\
    } catch (const std::exception& exception) {\
        caerLog(CAER_LOG_ERROR, #class "_" #function, "failed with C++ exception: %s", exception.what());\
    }\
}

#define BENCHMARK_WRAP_VOID_2(class, function, argument_type_0, argument_type_1)\
void class ## _ ## function(class* class ## _instance, argument_type_0 argument_0, argument_type_1 argument_1) {\
    try {\
        class ## _instance->function(argument_0, argument_1);\
    } catch (const std::exception& exception) {\
        caerLog(CAER_LOG_ERROR, #class "_" #function, "failed with C++ exception: %s", exception.what());\
    }\
}

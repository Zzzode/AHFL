add_library(ahfl_generated_parser STATIC
    generated/AHFLBaseVisitor.cpp
    generated/AHFLLexer.cpp
    generated/AHFLParser.cpp
    generated/AHFLVisitor.cpp
)

target_include_directories(ahfl_generated_parser
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src/parser/generated
)

ahfl_enable_cxx23(ahfl_generated_parser)
target_link_libraries(ahfl_generated_parser
    PUBLIC
        antlr4_runtime
)

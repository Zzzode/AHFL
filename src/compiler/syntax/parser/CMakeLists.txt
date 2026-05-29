add_library(ahfl_compiler_syntax_parser STATIC
    generated/AHFLLexer.cpp
    generated/AHFLParser.cpp
)

target_include_directories(ahfl_compiler_syntax_parser
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/compiler/syntax/parser/generated>
        $<INSTALL_INTERFACE:include/ahfl/compiler/syntax/parser/generated>
)

ahfl_enable_cxx23(ahfl_compiler_syntax_parser)
target_link_libraries(ahfl_compiler_syntax_parser
    PUBLIC
        antlr4_runtime
)

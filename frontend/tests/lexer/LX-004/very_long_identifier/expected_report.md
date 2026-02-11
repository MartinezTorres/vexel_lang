## Stdout
```
// Lowered Vexel module: tests/lexer/LX-004/very_long_identifier/test.vx
&^main() -> #i32 {
    this_is_a_very_long_identifier_name_that_should_be_accepted_by_the_lexer_because_there_is_no_length_limit_according_to_the_specification_and_we_need_to_verify_that_it_works_correctly_even_with_extremely_long_names_like_this_one: #i32 = 42;
    0
}
```

## Stderr
```
```

## Exit Code
0

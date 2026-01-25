## Stdout
```
// Lowered Vexel module: tests/declarations/DC-058/explicit_typename/test.vx
#Entity(state: #i32);
&(e)#Entity::update() -> #i32 {
    e.state = e.state + 1
}
&^main() -> #i32 {
    entity = Entity(0);
    Entity::update();
    entity.state
}
```

## Stderr
```
```

## Exit Code
0

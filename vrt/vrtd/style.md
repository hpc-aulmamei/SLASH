# Coding style

## Errors

There is no generally idiomatic way to do error handing in C. Each
project uses it's own convention.

Here is the convention for vrtd internal functions: functions that
can fail should return a sentinal value indicating faliure. This
sentinal value is `-1` for the `int` type.

If a function would be naturally `void`, but can fail, make it return
an `int` and return either `-1` (failure) or `0` success.

Do not try to
"optimize" by using integers of smaller sizes. As this value is
generally returned as a constant, checked immediatly after the return
and then never used again, it is safe to assume it will only ever live
in a register.

Do **NOT** return other negative values as failures, as the error
handling macros described bellow only use a comparison such as `== -1`
to check for error conditions.

If a function would naturally return something else, but it can fail, prefer to return
an integer, and pass the "return value" via a pointer instead, as bellow:

```C
double div(double x, double y)
{
    return x / y;
}

int div_safe(double x, double y, double *result)
{
    if (y == 0.0) {
        return -1;
    }

    if (result == NULL) {
        // Usually not an error if the caller doesn't want the result.
        // Otherwise we would do assert(result != NULL)

        return 0;
    }

    *result = x / y;

    return 0;
}
```

You should also do this in the case where `-1` is valid value for an `int`
result.

### Macros for internal error handling

We provide a set of macros to make error handling more ergonomic. The basic
macros are the `PROPAGATE_ERROR` and `GOTO_ON_ERROR`, and the practically more
common `PROPAGATE_ERROR` and `GOTO_ON_ERROR_LOG`.

#### `PROPAGATE_ERROR`

`PROPAGATE_ERROR` returns `-1` if and only if the provided argument is 

```C
int foo(void)
{
    int ret = bar();
    PROPAGATE_ERROR(bar);

    zee();

    return 0;
}
```


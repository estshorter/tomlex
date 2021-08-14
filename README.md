# tomlex
**tomlex** is a [toml11](https://github.com/ToruNiina/toml11) wrapper that provides similar functions to [OmegaConf](https://omegaconf.readthedocs.io/en/latest/).

Currently, the following functions are implemented.

1. `tomlex::from_cli()`
2. [Variable interpolation](https://omegaconf.readthedocs.io/en/latest/usage.html#interpolation)
3. [Custom resolvers](https://omegaconf.readthedocs.io/en/latest/custom_resolvers.html)

## Install
include the `include/tomlex/` directory in your project.
Note that this library requires C++17 or upper and depends on [toml11](https://github.com/ToruNiina/toml11).

## Usage
### simple usage
```cpp
#include <iostream>
#include <tomlex/tomlex.hpp>
#include <tomlex/resolvers.hpp>

toml::value plus_10(toml::value const& args) { return args.as_integer() + 10; };
using namespace std;

int main(void) {
	/* example.toml
	param = 9
	interp1 = "${param}"        # 9
	interp2 = "${a.b.c.d}"      # 10
	resolver = "${plus_10: 90}" # 100
	[a.b.c]
	d = 10
	*/
	tomlex::register_resolver("plus_10", plus_10);
	constexpr auto filename = "example.toml";
	toml::value cfg = tomlex::parse(filename);
	cout << cfg << endl;

	//This also works
	//toml::value cfg = toml::parse(filename);
	//cfg = tomlex::resolve(std::move(cfg));

	constexpr int argc = 3;
	constexpr char* argv[argc] = {"PROGRAM_PATH", "  param   =   10000  ", "a.b.c.d  =  nan"};
	toml::value cfg_cli = tomlex::from_cli(argc, argv);
	cfg_cli.as_table().merge(cfg.as_table()); // data except for param and a.b.c.d are merged into cfg_cli
	cout << cfg_cli << endl;

	tomlex::clear_resolvers();
}
```

### Variable interpolation
You can specify another value by "${dotted-key}".
Currently, an absolute path is allowed.

Examples:
```toml
job_id = "TEST"
param = 10
output_filepath = "output_${job_id}_${param}.bin" # "output_TEST_10.bin"
test = "${a.b.c.d}"                               # 10: int
#test = "${   a.b.c.d   }"                        # OK: leading and trailing spaces are allowed
#test = "${a.    b.c.d}"                          # NG: spaces before or after dot are NOT allowed
#test = "${'a.b.c.d'}"                            # NG: surrounding with quotation marks is NOT allowed

[a.b.c]
d = 10
# e = "${.d}" # NG: relative path is NOT allowed
```

If a string starts with "${" and ends with "}",
the type of the expanded value becomes the same as that of the reference value.
Otherwise, the type is a string.
```toml
param = 10
foo = "${param}"  # 10: int
bar = "${param}0" # "100": string, NOT int
```

### Custom resolvers
Examples:
```cpp
//function definition
toml::value join(toml::value const& args, std::string const& sep = "_") {
    switch (args.type()) {
        case toml::value_t::array: {
            auto& array_ = args.as_array();
            std::ostringstream oss;
            for (int i = 0; i < array_.size() - 1; i++) {
                oss << tomlex::detail::to_string(array_[i]) << sep;
            }
            oss << tomlex::detail::to_string(array_[array_.size() - 1]);
            return oss.str();
        }
        default:
            return args;
    }
}

//register
tomlex::register_resolver("join_", [](toml::value const& args) { return join(args); });
```

```toml
output_filepath =  "output_${join_: [10,20,30]}.bin"             # "output_0_10_20.bin"
#output_filepath2 =  "output_${   join   :   [10,20,30]  }.bin"  # OK: leading and trailing spaces are allowed
#output_filepath3 =  "output_${join: 10,20}.bin"                 # NG: 10,20 is not a valid toml value
#output_filepath4 =  "output_${'join': [10,20]}.bin"             # surrounding with quotation marks is NOT allowed
```
Note that arguments for the resolver (string after colon) must be a valid toml-value.
This is because an argument string (e.g. "[10,20,30]") is passed to a toml parser.

Also, please be careful of the type of arguments, because this library just replaces "${EXPR}" with the expanded string and parse it.

Examples:
``` toml
flt1 = 7.0
str1 = "7.0"

# function definition: 
# toml::value no_op(toml::value const& args) { return args; };

# be careful of argument type
conv_flt1 = "${no_op: ${flt1}}"   # ${no_op: 7.0} -> 7.0: double
conv_str1 = "${no_op: ${str1}}"   # ${no_op: 7.0} -> 7.0: double
conv_str2 = '${no_op: "${str1}"}' # ${no_op: "7.0"} -> "7.0": str
```
${fit1} and ${str1} are expanded as 7.0 and interpreted as float.
If you want to handle it as a string, please surround it with quotation marks 

As with variable interpolation, if a string starts with "${" and ends with "}", the type of the expanded value becomes the same as that of the return-value.
Otherwise, the type is a string.
```toml
ex1 = "${no_op: 7.0}"  #  7.0  : float
ex2 = "${no_op: 7.0}0" # "7.00": string
```

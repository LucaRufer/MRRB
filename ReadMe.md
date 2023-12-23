# Multi Reader Ring Buffer

MRRB is a C library that provides a simple and light-weight interface to fork incoming data from a single or multiple sources to multiple readers. For example, it allows re-targeting of 'printf' on an embedded to multiple sinks like ITM trace, a UART VCP, a log file or an ethernet socket at the same time with low overhead.

## Installation

All necessary files including an example can be found in this repository.

## Usage

*Coming soon*

## Example Project

An example project is written for the STMicroelectronics `NUCLEO-H723ZG` developement board. The project is set up using `STM32CubeMX` and supports building and debugging in `STM32CubeIDE` and `Visual Studio Code`.

The can be build using the `STM32CubeIDE` build configurations or using the supplied `Makefile`.

Loading and debugging the project in VSCode may require some additional steps:
1. If VSCode shows problems in the include path with system libraries like `<string.h>`, make sure that the path to the ARM system libraries is set correctly in `.vscode/c_cpp_properties.json`.
2. In order to load and debug the example project, you need to install [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html#st-get-software) and add the Command Line Interface (STM32_Programmer_CLI) to your path. On OSX, this can be done using:

```bash
cd /usr/local/bin
ln -s <Path to CLI> STM32_Programmer_CLI
```

When the ioc file is used to generate code with `STM32CubeMX`, some erroneous code is generated. In order to fix the errors in the LWIP drivers, use the `CodeGen/fix.sh` script to apply patches. The script is run automatically when building with `make`.

## Author

**Luca Rufer**
*[luca.rufer@swissloop.ch](mailto:luca.rufer@swissloop.ch)* - [LucaRufer](https://github.com/LucaRufer)

## License

The `Multi Reader Ring Buffer` library located in `Middlewares/Third_Party/MRRB` may be used under the MIT license.
For all other files, the Licenses shown in the respective folder or file header applies.
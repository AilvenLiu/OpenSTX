# IntelRDFPMathLib20U2

This is a slightly-modified version of the [Intel® Decimal Floating-Point Math Library](https://www.intel.com/content/www/us/en/developer/articles/tool/intel-decimal-floating-point-math-library.html#download).

The changes are as follows:

- A macOS-specific build script (**RUNOSX**) in the `LIBRARY` directory
- Another macOS-specific Makefile in `LIBRARY` (`macos-install.makefile`)

This repo is **specifically** for macOS (tested on arm64, likely will be just fine on x86_64). After running `RUNOSX` under the `LIBRARY` directory, run the following command:

```
$ sudo make install -f macos-install.makefile
```

And the correct version of the `libbid` library to be used for an upgraded version of the [twsapi](https://github.com/samkrishna/twsapi/tree/next-2023.1) library will be installed, along with headers.

## OTHER DETAILS:

Please see the [README](https://github.com/samkrishna/IntelRDFPMathLib20U2/blob/main/README) for additional details.

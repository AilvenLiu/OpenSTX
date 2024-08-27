CC = gcc
CFLAGS = -Wall -Wextra -Werror
LDFLAGS = -lm

INSTALL_DIR = /usr/local
INCLUDE_DIR = $(INSTALL_DIR)/include/bid
LIB_DIR = $(INSTALL_DIR)/lib

SRC_FILES = float128/dpml_erf_t.h \
            float128/dpml_special_exp.h \
            float128/dpml_globals.h \
            float128/architecture.h \
            float128/dpml_private.h \
            float128/dpml_log_t.h \
            float128/ix86_macros.h \
            float128/mphoc_functions.h \
            float128/dpml_lgamma_t.h \
            float128/dpml_bid_x.h \
            float128/compiler.h \
            float128/dpml_pow.h \
            float128/dpml_ux_32_64.h \
            float128/mphoc_macros.h \
            float128/build.h \
            float128/endian.h \
            float128/dpml_acosh_t.h \
            float128/dpml_cons_x.h \
            float128/dpml_rdx_x.h \
            float128/dpml_error_codes_enum.h \
            float128/f_format.h \
            float128/dpml_bessel_x.h \
            float128/dpml_function_info.h \
            float128/dpml_powi_x.h \
            float128/dpml_cbrt_x.h \
            float128/dpml_asinh_t.h \
            float128/dpml_inv_trig_x.h \
            float128/dpml_log_x.h \
            float128/dpml_erf_x.h \
            float128/dpml_error_codes.h \
            float128/i_format.h \
            float128/dpml_names.h \
            float128/dpml_lgamma_x.h \
            float128/dpml_trig_x.h \
            float128/dpml_log2_t.h \
            float128/dpml_mod_x.h \
            float128/dpml_ux_alpha_macros.h \
            float128/poly_macros.h \
            float128/dpml_pow_x.h \
            float128/dpml_sqrt_x.h \
            float128/dpml_inv_hyper_x.h \
            float128/mtc_macros.h \
            float128/dpml_exp_x.h \
            float128/dpml_int_x.h \
            float128/assert.h \
            float128/dpml_exception.h \
            float128/dpml_ux.h \
            float128/op_system.h \
            float128/sqrt_macros.h \
            src/bid_div_macros.h \
            src/dfp754.h \
            src/bid128_2_str.h \
            src/bid_conf.h \
            src/bid_inline_add.h \
            src/bid128_2_str_macros.h \
            src/bid_internal.h \
            src/bid_gcc_intrinsics.h \
            src/bid_functions.h \
            src/bid_b2d.h \
            src/bid_sqrt_macros.h \
            src/bid_wrap_names.h \
            src/bid_strtod.h \
            src/bid_trans.h

LIB_FILE = gcc011libbid.a

.PHONY: install

$(LIB_DIR)/libbid.a: $(LIB_FILE)
	cp $< $@

install: $(SRC_FILES) $(LIB_FILE)
	install -d $(INCLUDE_DIR)
	install -m 644 $(SRC_FILES) $(INCLUDE_DIR)
	cp $(LIB_FILE) $(LIB_DIR)/libbid.a

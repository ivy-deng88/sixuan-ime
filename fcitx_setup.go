package main

/*
#include <stdlib.h>
#include "native.h"
*/
import "C"

import (
	"errors"
	"os"
	"strings"
	"unsafe"
)

var fcitx5Desktop = func() bool {
	for _, key := range []string{"GTK_IM_MODULE", "QT_IM_MODULE"} {
		if strings.EqualFold(strings.TrimSpace(os.Getenv(key)), "fcitx") {
			return true
		}
	}
	if strings.Contains(strings.ToLower(os.Getenv("XMODIFIERS")), "@im=fcitx") {
		return true
	}
	_, err := runDesktopCommand("fcitx5-remote", "--check")
	return err == nil
}

var registerFcitx5 = func() error {
	p := C.native_fcitx_register()
	defer C.free(unsafe.Pointer(p))
	if message := C.GoString(p); message != "" {
		return errors.New(message)
	}
	return nil
}

// qt_event_source.cpp — non-template bridge source machinery (M3, §5.1).
//
// Part of the CRISP project. All template machinery of CtQtEventSource and
// detail::CtQtEventCapture lives in qt_event_source.h (language exception);
// this translation unit exists so that non-template logic
// (install/remove helpers) has a home and the header's template members get
// instantiated from a single TU when used by the bridge library.

#include "qt_event_source.h"

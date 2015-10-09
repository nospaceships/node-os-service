#ifndef SERVICE_H
#define SERVICE_H

/**
 ** The following warnings are displayed during compilation on win32 platforms
 ** using node-gyp:
 **
 **  - C++ exception handler used, but unwind semantics are not enabled.
 **  - no definition for inline function 'v8::Persistent<T> \
 **       v8::Persistent<T>::New(v8::Handle<T>)'
 **
 ** There don't seem to be any issues which would suggest these are real
 ** problems, so we've disabled them for now.
 **/
#ifdef _WIN32
#pragma warning(disable:4506;disable:4530)
#endif

#include <node.h>
#include <nan.h>

using namespace v8;

namespace service {

static void Init(Handle<Object> exports);

static NAN_METHOD(Add);
static NAN_METHOD(IsStopRequested);
static NAN_METHOD(Remove);
static NAN_METHOD(Run);
static NAN_METHOD(Stop);

}; /* namespace service */

#endif /* SERVICE_H */

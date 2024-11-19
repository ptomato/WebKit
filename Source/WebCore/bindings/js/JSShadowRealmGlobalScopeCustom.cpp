/*
 * Copyright (C) 2024 Igalia, S.L. All rights reserved.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"

#include "JSDOMExceptionHandling.h"
#include "JSShadowRealmGlobalScope.h"
#include <JavaScriptCore/JSCJSValue.h>

namespace WebCore {
using namespace JSC;

JSValue JSShadowRealmGlobalScope::queueMicrotask(JSGlobalObject& lexicalGlobalObject, CallFrame& callFrame)
{
    VM& vm = lexicalGlobalObject.vm();
    auto scope = DECLARE_THROW_SCOPE(vm);

    if (UNLIKELY(callFrame.argumentCount() < 1))
        return throwException(&lexicalGlobalObject, scope, createNotEnoughArgumentsError(&lexicalGlobalObject));

    JSValue functionValue = callFrame.uncheckedArgument(0);
    if (UNLIKELY(!functionValue.isCallable()))
        return JSValue::decode(throwArgumentMustBeFunctionError(lexicalGlobalObject, scope, 0, "callback"_s, "Window"_s, "queueMicrotask"_s));

    scope.release();
    Base::queueMicrotask(functionValue, jsUndefined(), jsUndefined(), jsUndefined(), jsUndefined());
    return jsUndefined();
}

}

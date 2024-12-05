/*
 * Copyright (C) 2021 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include "ShadowRealmGlobalScope.h"

#include "Base64Utilities.h"
#include "JSDOMGlobalObject.h"
#include "JSShadowRealmGlobalScope.h"
#include "MessagePort.h"
#include "ScriptModuleLoader.h"
#include "SerializedScriptValue.h"
#include <wtf/TZoneMallocInlines.h>

namespace WebCore {

WTF_MAKE_TZONE_OR_ISO_ALLOCATED_IMPL(ShadowRealmGlobalScope);

Ref<ShadowRealmGlobalScope> ShadowRealmGlobalScope::create(JSDOMGlobalObject* wrapper, ScriptModuleLoader* loader)
{
    return adoptRef(*new ShadowRealmGlobalScope(wrapper, loader));
}

ShadowRealmGlobalScope::ShadowRealmGlobalScope(JSDOMGlobalObject* wrapper, ScriptModuleLoader* loader)
    : m_incubatingWrapper(wrapper)
    , m_parentLoader(loader)
{
}

ScriptModuleLoader& ShadowRealmGlobalScope::moduleLoader()
{
    if (m_moduleLoader)
        return *m_moduleLoader;

    auto wrapper = m_wrapper.get();
    ASSERT(wrapper);

    m_moduleLoader = m_parentLoader->shadowRealmLoader(wrapper).moveToUniquePtr();
    return *m_moduleLoader;
}

ShadowRealmGlobalScope::~ShadowRealmGlobalScope() = default;

ExceptionOr<bool> ShadowRealmGlobalScope::isSecureContext() const {
    return scriptExecutionContext()->isSecureContext();
}

ExceptionOr<void> ShadowRealmGlobalScope::reportError(JSDOMGlobalObject& globalObject, JSC::JSValue error) {
    JSC::VM& vm = globalObject.vm();
    auto* exception = JSC::jsDynamicCast<JSC::Exception*>(error);
    if (!exception)
        exception = JSC::Exception::create(vm, error);

    reportException(&globalObject, exception);

    return { };
}

ExceptionOr<String> ShadowRealmGlobalScope::btoa(const String& stringToEncode)
{
    return Base64Utilities::btoa(stringToEncode);
}

ExceptionOr<String> ShadowRealmGlobalScope::atob(const String& stringToEncode)
{
    return Base64Utilities::atob(stringToEncode);
}

ExceptionOr<JSC::JSValue> ShadowRealmGlobalScope::structuredClone(JSDOMGlobalObject& lexicalGlobalObject, JSDOMGlobalObject& relevantGlobalObject, JSC::JSValue value, StructuredSerializeOptions&& options)
{
    Vector<Ref<MessagePort>> ports;
    auto messageData = SerializedScriptValue::create(lexicalGlobalObject, value, WTFMove(options.transfer), ports, SerializationForStorage::No, SerializationContext::WindowPostMessage);
    if (messageData.hasException())
        return messageData.releaseException();

    auto disentangledPorts = MessagePort::disentanglePorts(WTFMove(ports));
    if (disentangledPorts.hasException())
        return disentangledPorts.releaseException();

    Vector<Ref<MessagePort>> entangledPorts;
    if (auto* scriptExecutionContext = relevantGlobalObject.scriptExecutionContext())
        entangledPorts = MessagePort::entanglePorts(*scriptExecutionContext, disentangledPorts.releaseReturnValue());

    return messageData.returnValue()->deserialize(lexicalGlobalObject, &relevantGlobalObject, WTFMove(entangledPorts));
}

} // namespace WebCore

/* Copyright 2013-2021 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Ported from MultiMC launcher/net/Validator.h (Apache-2.0)

#pragma once

#include "NetAction.h"

namespace Net {
class Validator
{
public: /* con/des */
    Validator() {};
    virtual ~Validator() {};

public: /* methods */
    virtual bool init(QNetworkRequest & request) = 0;
    virtual bool write(QByteArray & data) = 0;
    virtual bool abort() = 0;
    virtual bool validate(QNetworkReply & reply) = 0;
};
}

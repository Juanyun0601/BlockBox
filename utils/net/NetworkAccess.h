// BlockBox glue for the MultiMC net layer (not part of upstream MultiMC).
// MultiMC reaches its process-wide QNetworkAccessManager through the
// Application singleton; BlockBox exposes it through this accessor.

#pragma once

#include <QNetworkAccessManager>

#include "QObjectPtr.h"

namespace Net
{

/// Process-wide network access manager shared by every NetJob.
inline shared_qobject_ptr<QNetworkAccessManager> networkAccessManager()
{
    static shared_qobject_ptr<QNetworkAccessManager> s_nam(new QNetworkAccessManager());
    return s_nam;
}

}

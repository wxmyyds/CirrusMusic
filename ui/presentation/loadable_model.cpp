#include "loadable_model.h"

#include "logging/log.h"

#include "app.h"
#include "diagnostics.h"
#include "network_status.h"
#include "session.h"

namespace cirrus::ui
{

LoadableModel::LoadableModel(QObject* parent) : QObject(parent)
{
    if (App* app = App::instance()) {
        connect(app->network(), &NetworkStatus::onlineChanged, this, [this, app]() {
            if (app->network()->online() && m_state == loadstate::Offline)
                retry();
        });
    }
}

void LoadableModel::followAccount()
{
    if (App* app = App::instance()) {
        connect(app->session(), &Session::accountChanged, this, [this]() {
            if (m_state != loadstate::Idle)
                reload();
        });
    }
}

void LoadableModel::retry()
{
    if (m_state == loadstate::Idle)
        return;
    if (hasContent()) {
        refresh();
        return;
    }
    setError({});
    setState(loadstate::Loading);
    start();
}

void LoadableModel::refresh()
{
    if (!hasContent()) {
        retry();
        return;
    }
    setRefreshing(true);
    start();
}

void LoadableModel::reload()
{
    m_request.cancel();
    clear();
    setError({});
    setRefreshing(false);
    setState(loadstate::Loading);
    start();
}

void LoadableModel::reset()
{
    m_request.cancel();
    clear();
    setError({});
    setRefreshing(false);
    setState(loadstate::Idle);
}

void LoadableModel::start()
{
    m_request.cancel();
    // Replies never arrive synchronously, so the handle is stored before any reply.
    m_request = load();
}

void LoadableModel::succeed(bool empty, bool partial)
{
    setError({});
    setRefreshing(false);
    setState(empty ? loadstate::Empty : partial ? loadstate::Partial : loadstate::Ready);
}

LoadError LoadableModel::describe(const Failure& failure, int* state) const
{
    LoadError error;
    error.kind = failure.kind;
    error.retryAfterMs = failure.retryAfterMs.value_or(0);
    int mapped = loadstate::Error;
    // No backend: nothing loads, whatever the request (and retrying won't help).
    if (const App* app = App::instance(); app && !app->startupProblem().isEmpty()) {
        error.message = app->startupProblem();
        if (state)
            *state = mapped;
        return error;
    }
    switch (failure.kind) {
    case failure::Network:
    case failure::Timeout: {
        const App* app = App::instance();
        if (app && !app->network()->online()) {
            mapped = loadstate::Offline;
            error.message = tr("网络不可用，恢复后会自动重试");
        } else {
            error.message = failure.kind == failure::Timeout ? tr("请求超时") : tr("网络连接失败");
        }
        error.retryable = true;
        break;
    }
    case failure::Busy:
    case failure::RateLimited:
        error.message = tr("服务繁忙，请稍后再试");
        error.retryable = true;
        break;
    case failure::SignedOut:
        mapped = loadstate::SignedOut;
        error.message = tr("登录后才能查看");
        break;
    case failure::Rejected:
        error.message = tr("内容暂时无法获取");
        break;
    case failure::Invalid:
        // A malformed request is our bug, not something the user can fix.
        log::warn(lcUi, "model.invalid_request",
                  {{"model", Diagnostics::typeName(this)},
                   {"ids", Diagnostics::ids(this, LoadableModel::staticMetaObject)}});
        error.message = tr("加载失败");
        break;
    case failure::Unknown:
        error.message = tr("加载失败");
        error.retryable = true;
        break;
    }
    if (state)
        *state = mapped;
    return error;
}

void LoadableModel::fail(const Failure& failure)
{
    // Every page or list that fails to load (docs/tasks/logging.md §8); signed out is
    // what a guest expects, not a fault.
    log::write(failure.kind == failure::SignedOut ? QtInfoMsg : QtWarningMsg, lcUi, "model.failed",
               {{"model", Diagnostics::typeName(this)},
                {"ids", Diagnostics::ids(this, LoadableModel::staticMetaObject)},
                {"kind", failure.kind},
                {"refreshing", m_refreshing},
                {"retry_after_ms", failure.retryAfterMs.value_or(0)}});
    int state = loadstate::Error;
    const LoadError error = describe(failure, &state);
    setError(error);
    // A failed refresh keeps the content; the page reports `error` on its own.
    const bool keepContent = m_refreshing && hasContent();
    setRefreshing(false);
    if (!keepContent)
        setState(state);
}

void LoadableModel::setState(int state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit stateChanged();
}

void LoadableModel::setRefreshing(bool refreshing)
{
    if (m_refreshing == refreshing)
        return;
    m_refreshing = refreshing;
    emit refreshingChanged();
}

void LoadableModel::setError(const LoadError& error)
{
    if (m_error == error)
        return;
    m_error = error;
    emit errorChanged();
}

bool LoadableModel::hasContent() const
{
    return m_state == loadstate::Ready || m_state == loadstate::Partial ||
           m_state == loadstate::Empty;
}

} // namespace cirrus::ui

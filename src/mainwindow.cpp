#include "mainwindow.h"
#include "resolver_entries.h"
#include <private/qdbusutil_p.h>
#include <QRegExp>

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    connection(QDBusConnection::systemBus())
{
    setSizePolicy(
                QSizePolicy(
                    QSizePolicy::MinimumExpanding,
                    QSizePolicy::MinimumExpanding));
    setWindowTitle("DNSCryptClient");
    QIcon::setThemeName("DNSCryptClient");
    setWindowIcon(QIcon::fromTheme(
                      "DNSCryptClient",
                      QIcon(":/DNSCryptClient.png")));
    setStyleSheet("QWidget {background-color: white;}");

    runAtStart = false;
    srvStatus = INACTIVE;
    findActiveService = false;
    stopManually = false;
    probeCount = 0;

    serverWdg = new ServerPanel(this);
    buttonsWdg = new ButtonPanel(this);
    infoWdg = new InfoPanel(this);
    baseLayout = new QVBoxLayout(this);
    baseLayout->addWidget(serverWdg, 1);
    baseLayout->addWidget(infoWdg, 3);
    baseLayout->addWidget(buttonsWdg, 5);
    baseWdg = new QWidget(this);
    baseWdg->setLayout(baseLayout);

    appSettings = new AppSettings(this);

    commonWdg = new QStackedWidget(this);
    commonWdg->addWidget(baseWdg);
    commonWdg->addWidget(appSettings);

    setCentralWidget(commonWdg);

    initTrayIcon();

    connect(serverWdg, SIGNAL(toSettings()),
            this, SLOT(toSettings()));
    connect(appSettings, SIGNAL(toBase()),
            this, SLOT(toBase()));
    connect(appSettings, SIGNAL(findActiveServiceStateChanged(bool)),
            this, SLOT(changeFindActiveServiceState(bool)));
    connect(buttonsWdg, SIGNAL(startProxing()),
            this, SLOT(startService()));
    connect(buttonsWdg, SIGNAL(stopProxing()),
            this, SLOT(stopService()));
    connect(buttonsWdg, SIGNAL(restoreSettings()),
            this, SLOT(restoreSystemSettings()));
    connect(this, SIGNAL(serviceStateChanged(SRV_STATUS)),
            this, SLOT(changeAppState(SRV_STATUS)));
    connect(this, SIGNAL(serviceStateChanged(SRV_STATUS)),
            buttonsWdg, SLOT(changeAppState(SRV_STATUS)));
    connect(this, SIGNAL(serviceStateChanged(SRV_STATUS)),
            serverWdg, SLOT(changeAppState(SRV_STATUS)));
    connect(this, SIGNAL(serviceStateChanged(SRV_STATUS)),
            infoWdg, SLOT(changeAppState(SRV_STATUS)));
    connect(serverWdg, SIGNAL(serverData(const QVariantMap&)),
            infoWdg, SLOT(setServerDescription(const QVariantMap&)));
    connect(serverWdg, SIGNAL(readyForStart()),
            this, SLOT(firstServiceStart()));
    connect(this, SIGNAL(nextServer()),
            this, SLOT(probeNextServer()));

    readSettings();
}

void MainWindow::readSettings()
{
    restoreGeometry(settings.value("Geometry").toByteArray());
    runAtStart = settings.value("RunAtStart", false).toBool();
    appSettings->setRunAtStartState(runAtStart);
    findActiveService = settings.value("FindActiveService", true).toBool();
    appSettings->setFindActiveServiceState(findActiveService);
    QString lastServer = settings.value("LastServer").toString();
    serverWdg->setLastServer(lastServer);
}
void MainWindow::setSettings()
{
    settings.setValue("Geometry", saveGeometry());
    settings.setValue("RunAtStart", appSettings->getRunAtStartState());
    settings.setValue("FindActiveService", findActiveService);
    settings.setValue("LastServer", serverWdg->getCurrentServer());
}
void MainWindow::initTrayIcon()
{
    trayIcon = new TrayIcon(this);
    connect(trayIcon, SIGNAL(activated(QSystemTrayIcon::ActivationReason)),
            this, SLOT(trayIconActivated(QSystemTrayIcon::ActivationReason)));
    connect(trayIcon->closeAction, SIGNAL(triggered(bool)),
            this, SLOT(close()));
    trayIcon->show();
}
void MainWindow::changeVisibility()
{
    ( this->isVisible() ) ? this->hide() : this->show();
}
void MainWindow::connectToClientService()
{
    if ( !connection.isConnected() ) {
        KNotification::event(
                    KNotification::Notification,
                    "DNSCryptClient",
                    QString("Not connected to org.freedesktop.systemd1"));
        return;
    };
    connection = QDBusConnection::systemBus();
    currentUnitTranscription.clear();
    currentUnitTranscription = serverWdg->getCurrentServer()
            .replace("-", "_2d").replace(".", "_2e");
    QString dbusPath = QString("DNSCryptClient_40%1_2eservice")
            .arg(currentUnitTranscription);
    bool connected = connection.connect(
                "org.freedesktop.systemd1",
                QString("/org/freedesktop/systemd1/unit/%1").arg(dbusPath),
                "org.freedesktop.DBus.Properties",
                "PropertiesChanged",
                this,
                SLOT(servicePropertyChanged(QDBusMessage)));
    const QString _state = ( connected )? "Connected" : "Not connected";
    KNotification::event(
                KNotification::Notification,
                "DNSCryptClient",
                QString("%1 for monitoring PropertiesChanged").arg(_state));
}
void MainWindow::disconnectFromClientService()
{
    if ( !connection.isConnected() ) {
        KNotification::event(
                    KNotification::Notification,
                    "DNSCryptClient",
                    QString("Not connected to org.freedesktop.systemd1"));
        return;
    };
    connection = QDBusConnection::systemBus();
    QString dbusPath = QString("DNSCryptClient_40%1_2eservice")
            .arg(currentUnitTranscription);
    bool disconnected = connection.disconnect(
                "org.freedesktop.systemd1",
                QString("/org/freedesktop/systemd1/unit/%1").arg(dbusPath),
                "org.freedesktop.DBus.Properties",
                "PropertiesChanged",
                this,
                SLOT(servicePropertyChanged(QDBusMessage)));
    const QString _state = ( disconnected )? "successful" : "failed";
    KNotification::event(
                KNotification::Notification,
                "DNSCryptClient",
                QString("Disconnection is %1").arg(_state));
}
void MainWindow::checkServiceStatus()
{
    QString dbusPath = QString("DNSCryptClient_40%1_2eservice")
            .arg(currentUnitTranscription);
    QDBusMessage msg = QDBusMessage::createMethodCall(
                "org.freedesktop.systemd1",
                QString("/org/freedesktop/systemd1/unit/%1").arg(dbusPath),
                "org.freedesktop.DBus.Properties",
                "Get");
    QVariantList _args;
    _args<<"org.freedesktop.systemd1.Unit"<<"ActiveState";
    msg.setArguments(_args);
    bool sent = connection.callWithCallback(
                msg, this, SLOT(receiveServiceStatus(QDBusMessage)));
    //return sent;
}
int MainWindow::checkSliceStatus()
{
    int ret;
    QDBusMessage msg = QDBusMessage::createMethodCall(
                "org.freedesktop.systemd1",
                "/org/freedesktop/systemd1/unit/system_2dDNSCryptClient_2eslice",
                "org.freedesktop.DBus.Properties",
                "Get");
    QVariantList _args;
    _args<<"org.freedesktop.systemd1.Slice"<<"TasksCurrent";
    msg.setArguments(_args);
    QDBusMessage answer = connection.call(msg);
    int currTasks;
    QRegExp rx("[0-9]{1,3}");
    QVariant arg;
    QString str;
    switch (answer.type()) {
    case QDBusMessage::ReplyMessage:
        arg = answer.arguments().first();
        str = QDBusUtil::argumentToString(arg);
        rx.indexIn(str);
        currTasks = rx.capturedTexts().first().toULongLong();
        if        ( currTasks==0 ) {
            // not active proxy
            ret = 0;
        } else if ( currTasks==1 ) {
            // one active proxy
            ret = 1;
        } else {
            // more than one active proxy
            // need to restart slice and proxing
            ret = -1;
        };
        break;
    case QDBusMessage::ErrorMessage:
        ret = -1;
    default:
        break;
    };
    return ret;
}
void MainWindow::startServiceProcess()
{
    connectToClientService();
    trayIcon->setIcon(
                QIcon::fromTheme("DNSCryptClient_reload",
                                 QIcon(":/reload.png")));
    QVariantMap args;
    Action act;
    switch (srvStatus) {
    //case INACTIVE:
    //    args["action"] = "create";
    //    act.setName("pro.russianfedora.dnscryptclient.create");
    //    break;
    case INACTIVE:
    case FAILED:
        args["action"] = "start";
        args["server"] = serverWdg->getCurrentServer();
        act.setName("pro.russianfedora.dnscryptclient.start");
        break;
    default:
        return;
    };
    act.setHelperId("pro.russianfedora.dnscryptclient");
    act.setArguments(args);
    ExecuteJob *job = act.execute();
    job->setAutoDelete(true);
    if (job->exec()) {
        QString code        = job->data().value("code").toString();
        QString msg         = job->data().value("msg").toString();
        QString err         = job->data().value("err").toString();
        QString entry       = job->data().value("entry").toString();
        addServerEnrty(entry);
        KNotification::event(
                   KNotification::Notification,
                   "DNSCryptClient",
                   QString("Session open with exit code: %1\nMSG: %2\nERR: %3")
                   .arg(code).arg(msg).arg(err));
    } else {
        KNotification::event(
                   KNotification::Notification,
                   "DNSCryptClient",
                   QString("ERROR: %1\n%2")
                   .arg(job->error()).arg(job->errorText()));
        trayIcon->setIcon(
                    QIcon::fromTheme("DNSCryptClient_closed",
                                     QIcon(":/closed.png")));
    };
}
void MainWindow::stopServiceProcess()
{
    QVariantMap args;
    args["action"] = "stop";
    args["server"] = serverWdg->getCurrentServer();
    Action act("pro.russianfedora.dnscryptclient.stop");
    act.setHelperId("pro.russianfedora.dnscryptclient");
    act.setArguments(args);
    ExecuteJob *job = act.execute();
    job->setAutoDelete(true);
    if (job->exec()) {
        QString code = job->data().value("code").toString();
        QString msg  = job->data().value("msg").toString();
        QString err  = job->data().value("err").toString();
        KNotification::event(
                   KNotification::Notification,
                   "DNSCryptClient",
                   QString("Session closed with exit code: %1\nMSG: %2\nERR: %3")
                   .arg(code).arg(msg).arg(err));
    } else {
        KNotification::event(
                   KNotification::Notification,
                   "DNSCryptClient",
                   QString("ERROR: %1\n%2")
                   .arg(job->error()).arg(job->errorText()));
        trayIcon->setIcon(
                    QIcon::fromTheme("DNSCryptClient_close",
                                     QIcon(":/close.png")));
    };
}
void MainWindow::stopSliceProcess()
{
    QVariantMap args;
    args["action"] = "stopslice";
    Action act("pro.russianfedora.dnscryptclient.stopslice");
    act.setHelperId("pro.russianfedora.dnscryptclient");
    act.setArguments(args);
    ExecuteJob *job = act.execute();
    job->setAutoDelete(true);
    if (job->exec()) {
        QString code = job->data().value("code").toString();
        QString msg  = job->data().value("msg").toString();
        QString err  = job->data().value("err").toString();
        KNotification::event(
                   KNotification::Notification,
                   "DNSCryptClient",
                   QString("Slice closed with exit code: %1\nMSG: %2\nERR: %3")
                   .arg(code).arg(msg).arg(err));
    } else {
        KNotification::event(
                   KNotification::Notification,
                   "DNSCryptClient",
                   QString("ERROR: %1\n%2")
                   .arg(job->error()).arg(job->errorText()));
        trayIcon->setIcon(
                    QIcon::fromTheme("DNSCryptClient_close",
                                     QIcon(":/close.png")));
    };
}
void MainWindow::findActiveServiceProcess()
{
    if ( srvStatus==FAILED || srvStatus==INACTIVE ) {
        if ( ++probeCount <= serverWdg->getServerListCount() ) {
            serverWdg->setNextServer();
            emit nextServer();
        } else {
            KNotification::event(
                        KNotification::Notification,
                        "DNSCryptClient",
                        "All servers probed and failed");
        };
    } else if ( srvStatus==ACTIVE || srvStatus==RESTORED ) {
        probeCount = 0;
    }
}
void MainWindow::addServerEnrty(const QString &entry)
{
    if ( entry.isEmpty() ) return;
    if ( resolverEntries.contains(entry) ) return;
    resolverEntries.append(entry);
}
QString MainWindow::showResolverEntries() const
{
    ResolverEntries *d = new ResolverEntries(serverWdg);
    d->setEntries(resolverEntries);
    d->exec();
    QString ret = d->getEntry();
    d->deleteLater();
    return ret;
}

/* private slots */
void MainWindow::toSettings()
{
    commonWdg->setCurrentWidget(appSettings);
}
void MainWindow::toBase()
{
    commonWdg->setCurrentWidget(baseWdg);
}
void MainWindow::firstServiceStart()
{
    // check running tasks in slice
    SRV_STATUS _srvStatus = FAILED;
    switch (checkSliceStatus()) {
    case 0 :
        // not exist
        _srvStatus = INACTIVE;
        disconnectFromClientService();
        break;
    case 1 :
    case -1:
    default:
        // TODO:
        // multiple exemplars or DNSCryptClient@.service running already
        // need to restart the slice and proxing or close another exemplars
        _srvStatus = RELOAD_SLICE;
        disconnectFromClientService();
        break;
    };
    emit serviceStateChanged(_srvStatus);
    if ( runAtStart ) {
        startService();
        runAtStart = false;
    };
}
void MainWindow::changeFindActiveServiceState(bool state)
{
    findActiveService = state;
}
void MainWindow::startService()
{
    stopManually = false;
    startServiceProcess();
    SRV_STATUS _srvStatus = FAILED;
    switch (checkSliceStatus()) {
    case 0 :
        // start failed
        _srvStatus = INACTIVE;
        disconnectFromClientService();
        break;
    case 1 :
        // start successful
        _srvStatus = ACTIVE;
        break;
    case -1:
    default:
        //  need to restart the slice and proxing
        _srvStatus = RELOAD_SLICE;
        disconnectFromClientService();
        break;
    };
    emit serviceStateChanged(_srvStatus);
}
void MainWindow::stopService()
{
    probeCount = 0;
    stopManually = true;
    stopServiceProcess();
    SRV_STATUS _srvStatus = FAILED;
    switch (checkSliceStatus()) {
    case 0 :
        // stop successful
        _srvStatus = INACTIVE;
        disconnectFromClientService();
        break;
    case 1 :
        // stop failed
        _srvStatus = ACTIVE;
        break;
    case -1:
    default:
        // need to restart the slice and proxing
        _srvStatus = RELOAD_SLICE;
        disconnectFromClientService();
        break;
    };
    emit serviceStateChanged(_srvStatus);
}
void MainWindow::restoreSystemSettings()
{
    stopService();
    QString selectedEntry = showResolverEntries();
    if ( selectedEntry.isEmpty() ) {
        selectedEntry = "nameserver 8.8.8.8\n";
    };
    QVariantMap args;
    args["action"] = "restore";
    args["entry"]  = selectedEntry;
    Action act("pro.russianfedora.dnscryptclient.restore");
    act.setHelperId("pro.russianfedora.dnscryptclient");
    act.setArguments(args);
    ExecuteJob *job = act.execute();
    job->setAutoDelete(true);
    if (job->exec()) {
        QString code = job->data().value("code").toString();
        QString msg  = job->data().value("msg").toString();
        QString err  = job->data().value("err").toString();
        KNotification::event(
                   KNotification::Notification,
                   "DNSCryptClient",
                   QString("Restore exit code: %1\nMSG: %2\nERR: %3")
                   .arg(code).arg(msg).arg(err));
        srvStatus = RESTORED;
    } else {
        KNotification::event(
                   KNotification::Notification,
                   "DNSCryptClient",
                   QString("ERROR: %1\n%2")
                   .arg(job->error()).arg(job->errorText()));
    };
    emit serviceStateChanged(srvStatus);
}
void MainWindow::trayIconActivated(QSystemTrayIcon::ActivationReason r)
{
    if ( r==QSystemTrayIcon::Trigger ) changeVisibility();
}
void MainWindow::servicePropertyChanged(QDBusMessage message)
{
    QVariantList args = message.arguments();
    if ( args.first().toString()!=
         "org.freedesktop.systemd1.Unit" ) return;
    checkServiceStatus();
}
void MainWindow::closeEvent(QCloseEvent *ev)
{
    if ( ev->type()==QEvent::Close ) {
        stopServiceProcess();
        setSettings();
        trayIcon->hide();
        ev->accept();
    };
}
void MainWindow::receiveServiceStatus(QDBusMessage _msg)
{
    QVariantList args = _msg.arguments();
    if ( args.length()!=1 ) return;
    QVariant arg = _msg.arguments().first();
    QString str = QDBusUtil::argumentToString(arg);
    QStringList l = str.split('"');
    if ( l.length()<3 ) return;
    QString status = l.at(1);

    KNotification::event(
                KNotification::Notification,
                "DNSCryptClient",
                QString("DNSCryptClient service is %1.").arg(status));

    if        ( status=="inactive" ) {
        srvStatus = INACTIVE;
    } else if ( status=="active" ) {
        srvStatus = ACTIVE;
    } else if ( status=="failed" ) {
        srvStatus = FAILED;
    } else if ( status=="activating" ) {
        srvStatus = ACTIVATING;
    } else if ( status=="deactivating" ) {
        srvStatus = DEACTIVATING;
    } else if ( status=="reloading" ) {
        srvStatus = RELOADING;
    } else {
        srvStatus = INACTIVE;
    };

    switch (srvStatus) {
    case INACTIVE:
    case FAILED:
        disconnectFromClientService();
        break;
    default:
        break;
    };
    emit serviceStateChanged(srvStatus);
}
void MainWindow::changeAppState(SRV_STATUS status)
{
    QTextStream s(stdout);
    switch ( status ) {
    case INACTIVE:
    case FAILED:
        s << "INACTIVE/FAILED" << endl;
        trayIcon->setIcon(
                    QIcon::fromTheme("DNSCryptClient_closed",
                                     QIcon(":/closed.png")));
        if ( !stopManually && findActiveService ) {
            findActiveServiceProcess();
        };
        break;
    case ACTIVE:
        trayIcon->setIcon(
                    QIcon::fromTheme("DNSCryptClient_opened",
                                     QIcon(":/opened.png")));
        s << "ACTIVE" << endl;
        break;
    case DEACTIVATING:
    case   ACTIVATING:
        trayIcon->setIcon(
                    QIcon::fromTheme("DNSCryptClient_reload",
                                     QIcon(":/reload.png")));
        s << "DEACTIVATING/ACTIVATING" << endl;
        break;
    case RELOAD_SLICE:
        // need to restart the slice and proxing
        s << "RELOAD_SLICE" << endl;
        stopSliceProcess();
    default:
        break;
    };
}
void MainWindow::probeNextServer()
{
    startService();
}

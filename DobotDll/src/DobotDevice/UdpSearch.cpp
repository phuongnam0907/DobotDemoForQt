#include "UdpSearch.h"
#include <QtNetwork>


// init the static values
UdpSearch       *UdpSearch::INSTANCE             = NULL;
const int        UdpSearch::BROADCAST_PORT       = 48899;
const QByteArray UdpSearch::BROADCAST_KEYWORD    = "Who is Dobot?";
const int        UdpSearch::DEVICE_LIFE_OVERTIME = 5;
const int        UdpSearch::LOCAL_PORT           = 2046;

UdpSearch *UdpSearch::Instance(QObject *parent)
{
    if (INSTANCE == 0) {
        INSTANCE = new UdpSearch(parent);
    }

    return INSTANCE;
}

UdpSearch::UdpSearch(QObject *parent)
    : QObject(parent)
    , m_checkingTimer(NULL)
    , m_socketListMutex()
    , m_socketMap()
    , m_signalMapper(NULL)
    , m_deviceListMutex()
    , m_deviceMap()
    , m_broadIP("")
{
    // clean up the containers
    m_socketMap.clear();
    m_deviceMap.clear();

    // setup signalMapper
    m_signalMapper = new QSignalMapper(this);
    connect(m_signalMapper, SIGNAL(mapped(QObject *)),
            this, SLOT(ProcessBroadCastDatagrams(QObject *)));

    // setup the timer
    m_checkingTimer = new QTimer(this);
    connect(m_checkingTimer, SIGNAL(timeout()), this, SLOT(OnCheckingTime()));
    m_checkingTimer->setSingleShot(true);
    m_checkingTimer->start(1);

    GetBroadIP();
    // search dobot via udp
    SearchDobotDevice();
}

UdpSearch::~UdpSearch()
{
    // release sockets
    foreach (QUdpSocket *socket, m_socketMap.values()) {
        if (socket) {
            socket->deleteLater();
            socket = NULL;
        }
    }

    // release timer
    if (m_checkingTimer) {
        m_checkingTimer->deleteLater();
        m_checkingTimer = NULL;
    }

    // release signalMapper
    if (m_signalMapper) {
        m_signalMapper->deleteLater();
        m_signalMapper = NULL;
    }
}

void UdpSearch::GetBroadIP()
{/*
    QString localIP;
    QList<QHostAddress>listAddress = QNetworkInterface::allAddresses();//定义一个容器用来装载IP地址
    for(int j = 0; j < listAddress.size(); j++)//设置循环,循环次数为listAddress的长度
    {
        if(!listAddress.at(j).isNull() && listAddress.at(j).protocol() ==
                QAbstractSocket::IPv4Protocol && listAddress.at(j) != QHostAddress::LocalHost)
        {
            localIP = listAddress.at(j).toString();//把IP地址转换为String类型,存放到a里面,此时a里面装的是本机的IP地址
            break;
        }
    }
    QStringList list = localIP.split('.');//把次IP地址,按'.'分割
    list.removeLast();//去掉最后一个
    list.append("255");//再在最后添加一个255
    m_broadIP = list.join('.');//把容器类转化为String类
    */

    //获取所有网络接口的列表
    foreach (QNetworkInterface netInterface, QNetworkInterface::allInterfaces())
    {
        QList<QNetworkAddressEntry> entryList = netInterface.addressEntries();

        //遍历每一个IP地址(每个包含一个IP地址，一个子网掩码和一个广播地址)
        foreach(QNetworkAddressEntry entry, entryList)
        {
            QString ip = entry.broadcast().toString();
            if (ip == "")
            {
                continue;
            }

            //广播地址
            m_broadIP = entry.broadcast().toString();
        }
    }

}

QStringList UdpSearch::SearchDobotDevice()
{
    //m_socketListMutex.lock();

    // send broadcast message
    foreach (QUdpSocket *socket, m_socketMap.values()) {

        socket->writeDatagram(BROADCAST_KEYWORD,
                              QHostAddress(m_broadIP),
                              BROADCAST_PORT);


        //qDebug() << QString("Broadcast ") + m_broadIP;
    }

    //m_socketListMutex.unlock();

    // return device list as the result of last searching
    return m_deviceMap.keys();
}

void UdpSearch::OnCheckingTime()
{
    SearchNetworkCard();
    DeviceLifeCheck();

    m_checkingTimer->start(1000);
}

void UdpSearch::ProcessBroadCastDatagrams(QObject *object)
{
    //m_socketListMutex.lock();

    QUdpSocket *socket =  (QUdpSocket*)object;

    QByteArray   datagram("");
    QHostAddress senderIP("0.0.0.0");

    while (socket->hasPendingDatagrams()) {
        datagram.resize(socket->pendingDatagramSize());

        // read data
        qint64 res = socket->readDatagram(datagram.data(),
                                          datagram.size(),
                                         &senderIP,
                                          NULL);

        if (res != -1) { // success of reading data
            // change QHostAddress to String
            QString targetIP(senderIP.toString());
            // change datagram to String
            QString dataStr(datagram.data());

            // check IP is in the begining of datagram or not
            if (dataStr.indexOf(targetIP) == 0) {
                DeviceListAdd(targetIP, socket->peerAddress().toString());
            }
        }
    }

    //m_socketListMutex.unlock();
}

void UdpSearch::DeviceListAdd(QString &device, QString localIP)
{
    m_deviceListMutex.lock();
    if (m_deviceMap.keys().indexOf(device) == -1) {
        m_deviceMap.insert(device, DEV_INFO(QTime::currentTime(), localIP));
        //qDebug((QString("Add device: ") + device).toLatin1().data());
    } else {
        //qDebug((QString("###########Living: ") + device).toLatin1().data());
        m_deviceMap[device].lifeTimeStamp = QTime::currentTime();
    }
    m_deviceListMutex.unlock();
}

void UdpSearch::DeviceLifeCheck()
{
    QTime       currentTime(QTime::currentTime());  // current time stamp
    QStringList deviceList(m_deviceMap.keys());     // device list of map

    foreach (QString device, deviceList) {
        if (m_deviceMap[device].lifeTimeStamp.secsTo(currentTime) >
            DEVICE_LIFE_OVERTIME) {

            // lost device feeback over life overtime
            m_deviceListMutex.lock();
            m_deviceMap.remove(device);
            m_deviceListMutex.unlock();

            //qDebug((QString("!!!!!!!!!!!!!!!!!!!!!!!Lost device: ") + device).toLatin1().data());
        }
    }
}

QString UdpSearch::GetLocalIP(QString &device)
{
    return m_deviceMap[device].localIP;
}

void UdpSearch::SearchNetworkCard()
{
    //m_socketListMutex.lock();
    QHostInfo           info(QHostInfo::fromName(QHostInfo::localHostName()));
    QList<QHostAddress> addressList(info.addresses());
    QStringList         localIPList(m_socketMap.keys());

    // check old network card is living or not
    foreach(QString localIP, localIPList) {
        bool localIPLiving(false);
        foreach(QHostAddress address, addressList) {
            if (address.toString() == localIP) {
                localIPLiving = true;
                break;
            }
        }
        if (!localIPLiving) {
            // lost network card, release its socket
            if (m_socketMap[localIP] != NULL) {
                m_socketMap[localIP]->deleteLater();
                m_socketMap[localIP] = NULL;
            }

            //qDebug("lost network card");
            m_socketMap.remove(localIP);
        }
    }

    // check new network
    int _tempPort = LOCAL_PORT;
    foreach(QHostAddress address, addressList) {
        if (address.protocol() != QAbstractSocket::IPv4Protocol) {
            continue;
        }

        bool isNewLocalIP(true);
        foreach (QString localIP, localIPList) {
            if (localIP == address.toString()) {
                isNewLocalIP = false;
                break;
            }
        }

        if (isNewLocalIP) {
            // new network card
            QUdpSocket *socket = new QUdpSocket(this);

            while(1){
                bool res = socket->bind(address, _tempPort);
                if (res) {
                    //qDebug() << "bind success!" << portTemp;
                    break;
                }
                else {
                    //qDebug() << "bind failed!" << portTemp;
                    ++_tempPort;
                }

                if (_tempPort > 49151) {
                    break;
                }

            }

            m_socketMap.insert(address.toString(), socket);

            m_signalMapper->setMapping(socket, (QObject*)socket);

            connect(socket, SIGNAL(readyRead()),
                    m_signalMapper, SLOT(map()));
        }
    }

    //m_socketListMutex.unlock();
}

void UdpSearch::Pacemaker(QString device)
{
    //qDebug((QString("\\\\\\\\\\\\\\\\Heart beat ") + device).toLatin1().data());
    m_deviceListMutex.lock();
    if (m_deviceMap.keys().indexOf(device) != -1) {
        m_deviceMap[device].lifeTimeStamp = QTime::currentTime();
    }
    m_deviceListMutex.unlock();
}

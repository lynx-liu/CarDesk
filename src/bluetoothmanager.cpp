#include "bluetoothmanager.h"
#include "t507sdkbridge.h"
#include <QDebug>

BluetoothManager::BluetoothManager(QObject *parent)
    : QObject(parent)
    , m_isConnected(false)
{
}

BluetoothManager::~BluetoothManager() {
}

void BluetoothManager::scanDevices() {
    qDebug() << "Scanning for Bluetooth devices...";
    
    // TODO: 优先按 T507 SDK 初始化蓝牙链路（xr829/xradio_btlpm + hciattach），再扫描设备。
    for (const QString &cmd : T507SdkBridge::bluetoothInitCommands()) {
        qDebug() << "[T507 SDK]" << cmd;
    }
    // 然后再用 DBus/BlueZ 扫描设备。
    
    emit scanFinished();
}

void BluetoothManager::connectDevice(const QString &deviceAddress) {
    qDebug() << "Connecting to Bluetooth device:" << deviceAddress;
    
    // TODO: 实现实际的蓝牙连接逻辑（优先对接 T507 SDK 推荐蓝牙栈）
    
    m_isConnected = true;
    emit deviceConnected(m_connectedDeviceName);
}

void BluetoothManager::disconnectDevice() {
    if (!m_isConnected) return;
    
    qDebug() << "Disconnecting from Bluetooth device";
    
    m_isConnected = false;
    emit deviceDisconnected();
}

bool BluetoothManager::isConnected() const {
    return m_isConnected;
}

QString BluetoothManager::getConnectedDeviceName() const {
    return m_connectedDeviceName;
}

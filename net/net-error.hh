#pragma once
#include <QAbstractSocket>
#include <QString>

namespace app
{
    namespace detail
    {
        /** @brief 把 QAbstractSocket::SocketError 转成日志用的短名 */
        inline QString socket_error_name(QAbstractSocket::SocketError err)
        {
            switch (err)
            {
                case QAbstractSocket::ConnectionRefusedError:           return QStringLiteral("ConnectionRefused");
                case QAbstractSocket::RemoteHostClosedError:            return QStringLiteral("RemoteHostClosed");
                case QAbstractSocket::HostNotFoundError:                return QStringLiteral("HostNotFound");
                case QAbstractSocket::SocketAccessError:                return QStringLiteral("SocketAccess");
                case QAbstractSocket::SocketResourceError:              return QStringLiteral("SocketResource");
                case QAbstractSocket::SocketTimeoutError:               return QStringLiteral("SocketTimeout");
                case QAbstractSocket::DatagramTooLargeError:            return QStringLiteral("DatagramTooLarge");
                case QAbstractSocket::NetworkError:                     return QStringLiteral("Network");
                case QAbstractSocket::AddressInUseError:                return QStringLiteral("AddressInUse");
                case QAbstractSocket::SocketAddressNotAvailableError:   return QStringLiteral("AddressNotAvailable");
                case QAbstractSocket::UnsupportedSocketOperationError:  return QStringLiteral("UnsupportedOperation");
                case QAbstractSocket::ProxyAuthenticationRequiredError: return QStringLiteral("ProxyAuthRequired");
                case QAbstractSocket::SslHandshakeFailedError:          return QStringLiteral("SslHandshakeFailed");
                case QAbstractSocket::UnfinishedSocketOperationError:   return QStringLiteral("UnfinishedOperation");
                case QAbstractSocket::ProxyConnectionRefusedError:      return QStringLiteral("ProxyConnectionRefused");
                case QAbstractSocket::ProxyConnectionClosedError:       return QStringLiteral("ProxyConnectionClosed");
                case QAbstractSocket::ProxyConnectionTimeoutError:      return QStringLiteral("ProxyConnectionTimeout");
                case QAbstractSocket::ProxyNotFoundError:               return QStringLiteral("ProxyNotFound");
                case QAbstractSocket::ProxyProtocolError:               return QStringLiteral("ProxyProtocol");
                case QAbstractSocket::OperationError:                   return QStringLiteral("Operation");
                case QAbstractSocket::SslInternalError:                 return QStringLiteral("SslInternal");
                case QAbstractSocket::SslInvalidUserDataError:          return QStringLiteral("SslInvalidUserData");
                case QAbstractSocket::TemporaryError:                   return QStringLiteral("Temporary");
                case QAbstractSocket::UnknownSocketError:               return QStringLiteral("Unknown");
            }
            return QStringLiteral("UnknownCode");
        }
    }
}

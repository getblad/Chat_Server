#include "LocalStorage_Service.h"
#include <qthread.h>
#include <QtConcurrent>

QSharedPointer<LocalStorage_Service> LocalStorage_Service::shp_instance = nullptr;
QMutex LocalStorage_Service::mutex;
QSharedPointer<QTimer> LocalStorage_Service::shp_timer = nullptr;

QList<QString> LocalStorage_Service::searchForFiles(
	const QDateTime& from_, const QDateTime& to_,
	const quint32& room_)
{
    QList<QString> file_names;
    QDir directory("rooms/" + QString::number(room_));
    QStringList all_files = directory.entryList(QDir::Files);
    bool is_empty = true;
    const QRegularExpression regex(R"((\d{8}_\d{4})\.json)"); // Regular expression to match file names like "20230102_1000.json"

    for (const QString& file_name : all_files)
    {
        if (QRegularExpressionMatch match = regex.match(file_name); match.hasMatch())
        {
            QString date_string = match.captured(1);

            if (QDateTime file_date_time = QDateTime::fromString(date_string, "yyyyMMdd_hhmm"); file_date_time >= from_ && file_date_time <= to_)
            {
                file_names.append(file_name);
                is_empty = false;
            }
        }
    }
    if (is_empty)
    {
        PLOGW << "No messages for this date was retrieved from database";
    }
    return file_names;

}

LocalStorage_Service* LocalStorage_Service::getInstance(int minutes_) {
    if (!shp_instance)
    {
        QMutexLocker locker(&mutex); 
        if (!shp_instance)
        {
            shp_instance = QSharedPointer<LocalStorage_Service>(new LocalStorage_Service(), &QObject::deleteLater);
            shp_timer = QSharedPointer<QTimer>(new QTimer(), &QObject::deleteLater);
            int timeout = minutes_ == 0? 5*60000 : minutes_ * 60000;
            connect(shp_instance.get(), &close, shp_instance.get(), &safeExit);
            connect(shp_timer.get(), &QTimer::timeout, shp_instance.get(), &LocalStorage_Service::deleteMessages);
            shp_timer->start(timeout);
        }

    }
    return shp_instance.get();
}

LocalStorage_Service::LocalStorage_Service(QObject* object_) : QObject(object_) {}


void LocalStorage_Service::saveAllMessages() {

    if (!shp_instance->message_storage.empty())
    {
        QMutexLocker locker(&mutex);
        auto keys = shp_instance->message_storage.keys();
        QtConcurrent::blockingMap(keys, [this](const quint32 key_) {
                QString current_time = QDateTime::currentDateTimeUtc().toString("yyyyMMdd_hhmm");
                QString file_name = "rooms/" + QString::number(key_) + "/" + current_time + ".json";
                QDir().mkpath("rooms/" + QString::number(key_));
                QJsonArray array;
                for (const auto& message : message_storage.value(key_)) {

                    array.append(message->toJson());
                }
                if (!FileRepository::writeJsonArr(file_name, array)) {
                    PLOGE << "Error writing to file";
                }
                PLOGI << "Writing messages successfully";

                shp_instance->message_storage.remove(key_);
        });
    }
    else PLOGI << "message_storage is empty.";
}

void LocalStorage_Service::deleteMessages()
{
    current_messages.clear();
}


void LocalStorage_Service::safeExit()
{
    saveAllMessages();
    shp_timer->stop();
    shp_timer->deleteLater();
    shp_instance->deleteLater();
    PLOGI << "Local storage service safely closed";
}


void LocalStorage_Service::addMessages(User_Message* message_, quint32 room_id_) {
    //if (!message_storage.contains(room_id_)) {
    //    QList<QSharedPointer<DBEntity::DBMessage>> new_room_history;
    //    message_storage.insert(room_id_, new_room_history);
    //}
    //if(!current_messages.contains(room_id_))
    //{
	   // 
    //}
    ////new DBEntity::DBMessage(message_) - заглушка
    //message_storage.value(room_id_).append(QSharedPointer<DBEntity::DBMessage>(new DBEntity::DBMessage(message_), &QObject::deleteLater));
}

void LocalStorage_Service::getMessages(const QDateTime& from_, const QDateTime& to_, const quint32& room_) {
    QSet<QSharedPointer<DBEntity::DBMessage>> messages;
    QMutexLocker locker(&mutex);

    const QDir directory("rooms/" + QString::number(room_));
    QStringList all_files = directory.entryList(QDir::Files);
    bool is_empty = true;
    const QRegularExpression regex(R"((\d{8}_\d{4})\.json)"); // Regular expression to match file names like "20230102_1000.json"
    auto future_read_files = QtConcurrent::map(all_files, [this, &regex, &messages, &is_empty, &from_, &to_](const QString& file_name_) {

        if (const QRegularExpressionMatch match = regex.match(file_name_); match.hasMatch())
        {
            const QString date_string = match.captured(1);

            if (const QDateTime file_date_time = QDateTime::fromString(date_string, "yyyyMMdd_hhmm"); file_date_time >= from_ && file_date_time <= to_)
            {
                messages.unite(DBEntity::DBMessage::readMessages(file_name_));
                is_empty = false;
            }
        }
          
    });
 
	
    if (current_messages.contains(room_))
    {
        auto future_read_map= QtConcurrent::map(current_messages.value(room_), [this, &messages, &from_, &to_](const auto& message_) {

            if (auto date = message_->getDateTime(); date >= from_ && date <= to_)
            {
                messages.insert(message_);
            }
        });
        future_read_map.waitForFinished();
        future_read_files.waitForFinished();
        current_messages[room_].unite(messages);
    }
    else
    {
        future_read_files.waitForFinished();
        current_messages.insert(room_, messages);
    }
     if (is_empty)
    {
        PLOGW << "No messages for this date was retrieved from database";
    }
    emit messageRetrieved(QList(messages.begin(), messages.end()));
    

}
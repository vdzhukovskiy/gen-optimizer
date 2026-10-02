#pragma once

#include <QObject>
#include <QString>

#include "core/map_data.h"

namespace core
{

// Session хранит состояние приложения и эмитит сигналы при его изменении.
// Ничего не знает про Qt Widgets и QGraphicsScene.
// UI подписывается на сигналы и решает, как реагировать.
class Session : public QObject
{
   Q_OBJECT

public:
   enum class State { Idle, Loading, Ready, Error };
   Q_ENUM(State)

   explicit Session(QObject* parent = nullptr);
   ~Session() override;

   // Загружает файл или директорию. filter — подстрока для отбора слоёв.
   void loadPath(const QString& path, const QString& filter = {});

   State          state() const noexcept { return state_; }
   const MapData& data()  const noexcept { return data_; }
   QString        lastError() const      { return last_error_; }

signals:
   void stateChanged(State s);
   void dataChanged();
   void errorOccurred(const QString& message);
   void statusMessage(const QString& message);

private:
   void setState(State s);

   State   state_ = State::Idle;
   MapData data_;
   QString last_error_;
};

} // namespace core
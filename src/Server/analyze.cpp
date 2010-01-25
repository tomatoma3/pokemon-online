#include "analyze.h"
#include "network.h"
#include "player.h"
#include "../PokemonInfo/pokemonstructs.h"
#include "../PokemonInfo/battlestructs.h"

using namespace NetworkServ;

Analyzer::Analyzer(QTcpSocket *sock) : mysocket(sock)
{
    connect(&socket(), SIGNAL(disconnected()), SIGNAL(disconnected()));
    connect(&socket(), SIGNAL(isFull(QByteArray)), this, SLOT(commandReceived(QByteArray)));
    connect(&socket(), SIGNAL(_error()), this, SLOT(error()));
    connect(this, SIGNAL(sendCommand(QByteArray)), &socket(), SLOT(send(QByteArray)));

    mytimer = new QTimer(this);
    connect(mytimer, SIGNAL(timeout()), this, SLOT(keepAlive()));
    mytimer->start(30000); //every 30 secs
}

void Analyzer::sendMessage(const QString &message)
{
    notify(SendMessage, message);
}

void Analyzer::engageBattle(int id, const TeamBattle &team, const BattleConfiguration &conf)
{
    notify(EngageBattle, id, team, conf);
}

void Analyzer::close() {
    socket().close();
}

QString Analyzer::ip() const {
    return socket().ip();
}

void Analyzer::sendPlayer(int num, const BasicInfo &team)
{
    notify(PlayersList, num, team);
}

void Analyzer::sendLogin(int num, const BasicInfo &team)
{
    notify(Login, num, team);
}

void Analyzer::sendLogout(int num)
{
    notify(Logout, num);
}

void Analyzer::keepAlive()
{
    notify(KeepAlive);
}

void Analyzer::sendChallengeStuff(quint8 stuff, int num)
{
    notify(ChallengeStuff, stuff, num);
}

void Analyzer::sendBattleResult(quint8 res)
{
    notify(BattleFinished, res);
}

void Analyzer::sendBattleCommand(const QByteArray & command)
{
    notify(BattleMessage, command);
}

void Analyzer::error()
{
    emit connectionError(socket().error(), socket().errorString());
}

bool Analyzer::isConnected() const
{
    return socket().isConnected();
}


void Analyzer::commandReceived(const QByteArray &commandline)
{
    QDataStream in (commandline);
    in.setVersion(QDataStream::Qt_4_5);
    uchar command;

    in >> command;

    switch (command) {
    case Login:
	{
	    TeamInfo team;
	    in >> team;
	    emit loggedIn(team);
	    break;
	}
    case SendMessage:
	{
	    QString mess;
	    in >> mess;
	    emit messageReceived(mess);
	    break;
	}
    case SendTeam:
	{
	    TeamInfo team;
	    in >> team;
	    emit teamReceived(team);
	    break;
	}
    case ChallengeStuff:
	{
	    quint8 stuff;
	    int id;
	    in >> stuff >> id;
	    emit challengeStuff(stuff, id);
	    break;
	}
    case BattleMessage:
	{
	    BattleChoice ch;
	    in >> ch;
	    emit battleMessage(ch);
	    break;
	}
    case BattleChat:
	{
	    QString s;
	    in >> s;
	    emit battleChat(s);
	    break;
	}
    case BattleFinished:
        emit forfeitBattle();
        break;
    case KeepAlive:
        break;
    case Register:
        emit wannaRegister();
        break;
    case AskForPass:
        {
            QString hash;
            in >> hash;
            emit sentHash(hash);
            break;
        }
    default:
        emit protocolError(UnknownCommand, tr("Protocol error: unknown command received"));
        break;
    }
}

Network & Analyzer::socket()
{
    return mysocket;
}

const Network & Analyzer::socket() const
{
    return mysocket;
}

void Analyzer::notify(int command)
{
    QByteArray tosend;
    QDataStream out(&tosend, QIODevice::WriteOnly);
    out.setVersion(QDataStream::Qt_4_5);

    out << uchar(command);

    emit sendCommand(tosend);
}

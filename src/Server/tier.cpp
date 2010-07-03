#include <cmath>
#include <ctime>
#include "../PokemonInfo/pokemoninfo.h"
#include "../PokemonInfo/battlestructs.h"
#include "tier.h"
#include "tiermachine.h"
#include "security.h"
#include "server.h"
#include "waitingobject.h"

QString MemberRating::toString() const
{
    return name + "%" + QString::number(matches).rightJustified(5,'0',true) + "%" +
            QString::number(rating).rightJustified(5,'0',true);
}

void MemberRating::changeRating(int opponent_rating, bool win)
{
    QPair<int,int> change = pointChangeEstimate(opponent_rating);

    matches += 1;
    rating = rating + (win ? change.first : change.second);
}

QPair<int, int> MemberRating::pointChangeEstimate(int opponent_rating)
{
    int n = matches;

    int kfactor;
    if (n <= 5) {
        static const int kfactors[] = {200, 100, 80, 70, 60, 50};
        kfactor = kfactors[n];
    } else {
        kfactor = 32;
    }
    double myesp = 1/(1+ pow(10., (float(opponent_rating)-rating)/400));

    return QPair<int,int>((1. - myesp)*kfactor,(0. - myesp)*kfactor);
}

void Tier::changeName(const QString &name)
{
    this->m_name = name;
    this->sql_table = "tier_" + ::slug(name);
}

QString Tier::name() const
{
    return m_name;
}

void Tier::changeId(int id)
{
    m_id = id;
}

int Tier::make_query_number(int type)
{
    return (type << 16) + id();
}

void Tier::loadFromFile()
{
    QSqlQuery query;

    query.setForwardOnly(true);

    query.exec(QString("select * from %1 limit 1").arg(sql_table));

    if (!query.next()) {
        if (SQLCreator::databaseType == SQLCreator::PostGreSQL) {
            /* The only way to have an auto increment field with PostGreSQL is to my knowledge using the serial type */
            query.exec(QString("create table %1 (id serial, name varchar(20), rating int, matches int, primary key(id))").arg(sql_table));
        } else if (SQLCreator::databaseType == SQLCreator::MySQL){
            query.exec(QString("create table %1 (id integer auto_increment, name varchar(20) unique, rating int, matches int, primary key(id))").arg(sql_table));
        } else if (SQLCreator::databaseType == SQLCreator::SQLite){
            /* The only way to have an auto increment field with SQLite is to my knowledge having a 'integer primary key' field -- that exact quote */
            query.exec(QString("create table %1 (id integer primary key autoincrement, name varchar(20) unique, rating int, matches int)").arg(sql_table));
        } else {
            throw QString("Using a not supported database");
        }

        query.exec(QString("create index tiername_index on %1 (name)").arg(sql_table));
        query.exec(QString("create index tierrating_index on %1 (rating)").arg(sql_table));

        Server::print(QString("Importing old database for tier %1 to table %2").arg(name(), sql_table));

        QFile in("tier_" + name() + ".txt");
        in.open(QIODevice::ReadOnly);

        QStringList members = QString::fromUtf8(in.readAll()).split('\n');

        clock_t t = clock();

        query.prepare(QString("insert into %1(name, rating, matches) values (:name, :rating, :matches)").arg(sql_table));

        QSqlDatabase::database().transaction();

        foreach(QString member, members) {
            QString m2 = member.toLower();
            QStringList mmr = m2.split('%');
            if (mmr.size() != 3)
                continue;

            query.bindValue(":name", mmr[0]);
            query.bindValue(":matches", mmr[1].toInt());
            query.bindValue(":rating", mmr[2].toInt());

            query.exec();
        }
        
        QSqlDatabase::database().commit();

        t = clock() - t;

        Server::print(QString::number(float(t)/CLOCKS_PER_SEC) + " secs");
        Server::print(query.lastError().text());
    }
}

QString Tier::toString() const {
    QString ret = name() + "=";

    if (parent.length() > 0) {
        ret += parent + "+";
    }

    if (bannedPokes.count() > 0) {
        foreach(BannedPoke pok, bannedPokes) {
            ret += PokemonInfo::Name(pok.poke) + (pok.item == 0 ? "" : QString("@%1").arg(ItemInfo::Name(pok.item))) + ", ";
        }
        ret.resize(ret.length()-2);
    }

    return ret;
}

void Tier::fromString(const QString &s) {
    changeName("");
    parent.clear();
    bannedPokes.clear();
    bannedPokes2.clear();

    QStringList s2 = s.split('=');
    if (s2.size() > 1) {
        changeName(s2[0]);
        QStringList rest = s2[1].split('+');
        QStringList pokes;

        if (rest.length() > 1) {
            parent = rest[0];
            pokes = rest[1].split(',');
        } else {
            pokes = rest[0].split(',');
        }

        foreach(QString poke, pokes) {
            BannedPoke pok = BannedPoke(PokemonInfo::Number(poke.section('@',0,0).trimmed()), ItemInfo::Number(poke.section('@',1,1).trimmed()));
            if (pok.poke != 0) {
                bannedPokes.push_back(pok);
                bannedPokes2.insert(pok.poke, pok);
            }
        }
    }
}

int Tier::count()
{
    QSqlQuery q;
    q.setForwardOnly(true);

    q.exec(QString("select count(*) from %1").arg(sql_table));

    q.next();
    return q.value(0).toInt();
}

bool Tier::isBanned(const PokeBattle &p) const {
    if (bannedPokes2.contains(p.num())) {
        QList<BannedPoke> values = bannedPokes2.values(p.num());

        foreach (BannedPoke b, values) {
            if (b.item == 0 || b.item == p.item())
                return true;
        }
    }
    if (parent.length() > 0 && boss != NULL) {
        return boss->tier(parent).isBanned(p);
    } else {
        return false;
    }
}

bool Tier::exists(const QString &name)
{
    if (!holder.isInMemory(name))
        loadMemberInMemory(name);
    return holder.exists(name);
}

int Tier::ranking(const QString &name)
{
    Server::print(QString("Ranking for %1").arg(name));
    if (!exists(name))
        return -1;
    int r = rating(name);
    QSqlQuery q;
    q.setForwardOnly(true);
    q.prepare(QString("select count(*) from %1 where (rating>:r1 or (rating=:r2 and name<=:name))").arg(sql_table));
    q.bindValue(":r1", r);
    q.bindValue(":r2", r);
    q.bindValue(":name", name);
    q.exec();

    if (q.next())
        return q.value(0).toInt();
    else
        return -1;
}

bool Tier::isValid(const TeamBattle &t)  const
{
    for (int i = 0; i< 6; i++)
        if (t.poke(i).num() != 0 && isBanned(t.poke(i)))
            return false;

    return true;
}

void Tier::changeRating(const QString &player, int newRating)
{
    MemberRating m = member(player);
    m.rating = newRating;

    updateMember(m);
}

void Tier::changeRating(const QString &w, const QString &l)
{
    /* Not really necessary, as pointChangeEstimate should always be called
       at the beginning of the battle, but meh maybe it's not a battle */
    bool addw(false), addl(false);
    addw = !exists(w);
    addl = !exists(l);

    MemberRating win = addw ? MemberRating(w) : member(w);
    MemberRating los = addl ? MemberRating(l) : member(l);

    int oldwin = win.rating;
    win.changeRating(los.rating, true);
    los.changeRating(oldwin, false);

    updateMember(win, addw);
    updateMember(los, addl);
}

MemberRating Tier::member(const QString &name)
{
    if (!holder.isInMemory(name))
        loadMemberInMemory(name);

    return holder.member(name);
}

int Tier::rating(const QString &name)
{
    if (!holder.isInMemory(name))
        loadMemberInMemory(name);
    if (exists(name)) {
        return holder.member(name).rating;
    } else {
        return 1000;
    }
}

void Tier::loadMemberInMemory(const QString &name, QObject *o, const char *slot)
{
    QString n2 = name.toLower();

    if (o == NULL) {
        if (holder.isInMemory(n2))
            return;

        QSqlQuery q;
        q.setForwardOnly(true);
        processQuery(&q, n2, GetInfoOnUser);

        return;
    }

    holder.cleanCache();

    WaitingObject *w = WaitingObjects::getObject();

    QObject::connect(w, SIGNAL(waitFinished()), o, slot);

    if (holder.isInMemory(n2)) {
        w->emitSignal();
        WaitingObjects::freeObject(w);
    }
    else {
        WaitingObjects::useObject(w);
        QObject::connect(w, SIGNAL(waitFinished()), WaitingObjects::getInstance(), SLOT(freeObject()));

        LoadThread *t = getThread();

        t->pushQuery(n2, w, make_query_number(GetInfoOnUser));
    }
}

/* Precondition: name is in lowercase */
void Tier::processQuery(QSqlQuery *q, const QString &name, int type)
{
    if (type == GetInfoOnUser) {
        q->prepare(QString("select matches, rating from %1 where name=? limit 1").arg(sql_table));
        q->addBindValue(name);
        q->exec();
        if (!q->next()) {
            holder.addNonExistant(name);
        } else {
            MemberRating m(name, q->value(0).toInt(), q->value(1).toInt());
            holder.addMemberInMemory(m);
        }
        q->finish();
    }
}

void Tier::insertMember(QSqlQuery *q, void *data, int update)
{
    MemberRating *m = (MemberRating*) data;

    if (update)
        q->prepare(QString("update %1 set matches=:matches, rating=:rating where name=:name").arg(sql_table));
    else
        q->prepare(QString("insert into %1(name, matches, rating) values(:name, :matches, :rating)").arg(sql_table));

    q->bindValue(":name", m->name);
    q->bindValue(":matches", m->matches);
    q->bindValue(":rating", m->rating);

    q->exec();
    q->finish();
}

void Tier::updateMember(const MemberRating &m, bool add)
{
    holder.addMemberInMemory(m);

    updateMemberInDatabase(m, add);
}

void Tier::updateMemberInDatabase(const MemberRating &m, bool add)
{
    boss->ithread->pushMember(m, make_query_number(int(!add)));
}

Tier::Tier(TierMachine *boss) : boss(boss), holder(1000){

}

QPair<int, int> Tier::pointChangeEstimate(const QString &player, const QString &foe)
{
    MemberRating p = exists(player) ? member(player) : MemberRating(player);
    MemberRating f = exists(foe) ? member(foe) : MemberRating(foe);

    return p.pointChangeEstimate(f.rating);
}

LoadThread * Tier::getThread()
{
    return boss->getThread();
}

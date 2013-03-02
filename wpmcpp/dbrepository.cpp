#include "dbrepository.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QDir>
#include <QVariant>
#include <QDomDocument>
#include <QDomElement>
#include <QTextStream>
#include <QByteArray>
#include <QDebug>

#include "package.h"
#include "repository.h"
#include "packageversion.h"
#include "wpmutils.h"
#include "installedpackages.h"

static bool packageVersionLessThan3(const PackageVersion* a,
        const PackageVersion* b)
{
    int r = a->package.compare(b->package);
    if (r == 0) {
        r = a->version.compare(b->version);
    }

    return r > 0;
}

class QMySqlQuery: public QSqlQuery {
public:
    bool exec(const QString& query);
    bool exec();
};

bool QMySqlQuery::exec(const QString &query)
{
    //DWORD start = GetTickCount();
    bool r = QSqlQuery::exec(query);
    // qDebug() << query << (GetTickCount() - start);
    return r;
}

bool QMySqlQuery::exec()
{
    //DWORD start = GetTickCount();
    bool r = QSqlQuery::exec();
    // qDebug() << this->lastQuery() << (GetTickCount() - start);
    return r;
}

DBRepository DBRepository::def;

DBRepository::DBRepository()
{
}

DBRepository::~DBRepository()
{
}

DBRepository* DBRepository::getDefault()
{
    return &def;
}

QString DBRepository::exec(const QString& sql)
{
    QMySqlQuery q;
    q.exec(sql);
    return toString(q.lastError());
}

QString DBRepository::insertPackage(Package* p)
{
    QMySqlQuery q;
    q.prepare("INSERT INTO PACKAGE "
            "(ID, NAME, TITLE, URL, ICON, DESCRIPTION, LICENSE, FULLTEXT)"
            "VALUES(:ID, :NAME, :TITLE, :URL, :ICON, :DESCRIPTION, :LICENSE, "
            ":FULLTEXT)");
    q.bindValue(":ID", QVariant(QVariant::Int));
    q.bindValue(":NAME", p->name);
    q.bindValue(":TITLE", p->title);
    q.bindValue(":URL", p->url);
    q.bindValue(":ICON", p->icon);
    q.bindValue(":DESCRIPTION", p->description);
    q.bindValue(":LICENSE", p->license);
    q.bindValue(":FULLTEXT", (p->title + " " + p->description + " " +
            p->name).toLower());
    q.exec();
    return toString(q.lastError());
}

QString DBRepository::insertPackageVersion(PackageVersion* p)
{
    QMySqlQuery q;
    q.prepare("INSERT INTO PACKAGE_VERSION "
            "(ID, NAME, PACKAGE, CONTENT, MSIGUID)"
            "VALUES(:ID, :NAME, :PACKAGE, :CONTENT, :MSIGUID)");
    q.bindValue(":ID", QVariant(QVariant::Int));
    q.bindValue(":NAME", p->version.getVersionString());
    q.bindValue(":PACKAGE", p->package);
    q.bindValue(":MSIGUID", p->msiGUID);
    QDomDocument doc;
    QDomElement root = doc.createElement("version");
    doc.appendChild(root);
    p->toXML(&root);
    QByteArray file;
    QTextStream s(&file);
    doc.save(s, 4);

    q.bindValue(":CONTENT", QVariant(file));
    q.exec();
    return toString(q.lastError());
}

QString DBRepository::insertLicense(License* p)
{
    QMySqlQuery q;
    q.prepare("INSERT INTO LICENSE "
            "(ID, NAME, TITLE, DESCRIPTION, URL)"
            "VALUES(:ID, :NAME, :TITLE, :DESCRIPTION, :URL)");
    q.bindValue(":ID", QVariant(QVariant::Int));
    q.bindValue(":NAME", p->name);
    q.bindValue(":TITLE", p->title);
    q.bindValue(":DESCRIPTION", p->description);
    q.bindValue(":URL", p->url);
    q.exec();
    return toString(q.lastError());
}

bool DBRepository::tableExists(QSqlDatabase* db,
        const QString& table, QString* err)
{
    *err = "";
    QMySqlQuery q;
    q.prepare("SELECT name FROM sqlite_master WHERE "
            "type='table' AND name=:NAME");
    q.bindValue(":NAME", table);
    q.exec();
    *err = toString(q.lastError());

    bool e = false;
    if (err->isEmpty()) {
        e = q.next();
    }
    return e;
}

Package *DBRepository::findPackage_(const QString &name)
{
    Package* r = 0;

    QMySqlQuery q;
    q.prepare("SELECT ID, NAME, TITLE, URL, ICON, "
            "DESCRIPTION, LICENSE FROM PACKAGE WHERE NAME = :NAME");
    q.bindValue(":NAME", name);
    q.exec();
    if (q.next()) {
        Package* p = new Package(name, q.value(2).toString());
        p->url = q.value(3).toString();
        p->icon = q.value(4).toString();
        p->description = q.value(5).toString();
        p->license = q.value(6).toString();
        r = p;
    }

    return r;
}

PackageVersion* DBRepository::findPackageVersion_(
        const QString& package, const Version& version)
{
    QString version_ = version.getVersionString();
    PackageVersion* r = 0;

    QMySqlQuery q;
    q.prepare("SELECT ID, NAME, "
            "PACKAGE, CONTENT, MSIGUID FROM PACKAGE_VERSION "
            "WHERE NAME = :NAME AND PACKAGE = :PACKAGE");
    q.bindValue(":NAME", version_);
    q.bindValue(":PACKAGE", package);
    q.exec();
    if (q.next()) {
        // TODO: handle error
        QDomDocument doc;
        int errorLine, errorColumn;
        QString err;
        if (!doc.setContent(q.value(3).toByteArray(),
                &err, &errorLine, &errorColumn))
            err = QString(
                    "XML parsing failed at line %1, column %2: %3").
                    arg(errorLine).arg(errorColumn).arg(err);

        QDomElement root = doc.documentElement();
        PackageVersion* p = PackageVersion::parse(&root, &err);

        // TODO: handle this error
        if (err.isEmpty()) {
            r = p;
        }
    }

    return r;
}

QList<PackageVersion*> DBRepository::getPackageVersions_(
        const QString& package, QString* err) const
{
    *err = "";

    QList<PackageVersion*> r;

    QMySqlQuery q;
    q.prepare("SELECT ID, NAME, "
            "PACKAGE, CONTENT, MSIGUID FROM PACKAGE_VERSION "
            "WHERE PACKAGE = :PACKAGE");
    q.bindValue(":PACKAGE", package);
    if (!q.exec()) {
        *err = toString(q.lastError());
    }

    while (err->isEmpty() && q.next()) {
        QDomDocument doc;
        int errorLine, errorColumn;
        if (!doc.setContent(q.value(3).toByteArray(),
                err, &errorLine, &errorColumn)) {
            *err = QString(
                    "XML parsing failed at line %1, column %2: %3").
                    arg(errorLine).arg(errorColumn).arg(*err);
        }

        QDomElement root = doc.documentElement();

        if (err->isEmpty()) {
            PackageVersion* pv = PackageVersion::parse(&root, err);
            if (err->isEmpty())
                r.append(pv);
        }
    }

    // qDebug() << vs.count();

    qSort(r.begin(), r.end(), packageVersionLessThan3);

    return r;
}

License *DBRepository::findLicense_(const QString& name)
{
    License* r = 0;
    License* cached = this->licenses.object(name);
    if (!cached) {
        QMySqlQuery q;
        q.prepare("SELECT ID, NAME, TITLE, DESCRIPTION, URL "
                "FROM LICENSE "
                "WHERE NAME = :NAME");
        q.bindValue(":NAME", name);
        q.exec();
        if (q.next()) {
            // TODO: handle error
            cached = new License(name, q.value(2).toString());
            cached->description = q.value(3).toString();
            cached->url = q.value(4).toString();
            r = cached->clone();
            this->licenses.insert(name, cached);
        }
    } else {
        r = cached->clone();
    }

    return r;
}

QList<Package*> DBRepository::findPackages(const QString& query) const
{
    // TODO: errors are not handled
    QStringList keywords = query.toLower().simplified().split(" ",
            QString::SkipEmptyParts);

    QList<Package*> r;

    QMySqlQuery q;
    QString sql = "SELECT ID, NAME, TITLE, URL, ICON, "
            "DESCRIPTION, LICENSE FROM PACKAGE";
    for (int i = 0; i < keywords.count(); i++) {
        if (i == 0)
            sql += " WHERE";
        else
            sql += " AND";
        sql += QString(" FULLTEXT LIKE :FULLTEXT%1").arg(i);
    }
    sql += " ORDER BY TITLE";
    // qDebug() << sql;
    q.prepare(sql);
    for (int i = 0; i < keywords.count(); i++) {
        q.bindValue(QString(":FULLTEXT%1").arg(i),
                "%" + keywords.at(i).toLower() + "%");
    }
    q.exec();
    while (q.next()) {
        Package* p = new Package(q.value(1).toString(), q.value(2).toString());
        p->url = q.value(3).toString();
        p->icon = q.value(4).toString();
        p->description = q.value(5).toString();
        p->license = q.value(6).toString();
        r.append(p);
    }

    return r;
}

QList<PackageVersion *> DBRepository::findPackageVersions() const
{
    // TODO: errors are not handled

    QList<PackageVersion*> r;

    QMySqlQuery q;
    q.prepare("SELECT ID, NAME, "
            "PACKAGE, CONTENT, MSIGUID FROM PACKAGE_VERSION");
    q.exec();
    while (q.next()) {
        // TODO: handle error
        QDomDocument doc;
        int errorLine, errorColumn;
        QString err;
        if (!doc.setContent(q.value(3).toByteArray(),
                &err, &errorLine, &errorColumn))
            err = QString(
                    "XML parsing failed at line %1, column %2: %3").
                    arg(errorLine).arg(errorColumn).arg(err);

        QDomElement root = doc.documentElement();
        PackageVersion* p = PackageVersion::parse(&root, &err);

        // TODO: handle this error
        if (err.isEmpty()) {
            r.append(p);
        }
    }

    return r;
}

QString DBRepository::savePackage(Package *p)
{
    QString r;

    Package* fp = findPackage_(p->name);
    if (fp) {
        delete fp;

        QMySqlQuery q;
        q.prepare("UPDATE PACKAGE "
                "SET TITLE=:TITLE, URL=:URL, ICON=:ICON, "
                "DESCRIPTION=:DESCRIPTION, LICENSE=:LICENSE, "
                "FULLTEXT=:FULLTEXT "
                "WHERE NAME=:NAME");
        q.bindValue(":TITLE", p->title);
        q.bindValue(":URL", p->url);
        q.bindValue(":ICON", p->icon);
        q.bindValue(":DESCRIPTION", p->description);
        q.bindValue(":LICENSE", p->license);
        q.bindValue(":FULLTEXT", (p->title + " " + p->description + " " +
                p->name).toLower());
        q.bindValue(":NAME", p->name);
        q.exec();
        r = toString(q.lastError());
    } else {
        r = insertPackage(p);
    }

    return r;
}

QString DBRepository::savePackageVersion(PackageVersion *p)
{
    QString r;

    PackageVersion* fp = findPackageVersion_(p->package, p->version);
    if (fp) {
        delete fp;

        QMySqlQuery q;
        q.prepare("UPDATE PACKAGE_VERSION "
                "SET CONTENT=:CONTENT, "
                "MSIGUID=:MSIGUID "
                "WHERE PACKAGE=:PACKAGE AND NAME=:NAME");
        q.bindValue(":NAME", p->version.getVersionString());
        q.bindValue(":PACKAGE", p->package);
        q.bindValue(":MSIGUID", p->msiGUID);
        QDomDocument doc;
        QDomElement root = doc.createElement("version");
        doc.appendChild(root);
        p->toXML(&root);
        QByteArray file;
        QTextStream s(&file);
        doc.save(s, 4);
        q.bindValue(":CONTENT", QVariant(file));
        q.exec();
        r = toString(q.lastError());
    } else {
        r = insertPackageVersion(p);
    }

    return r;
}

PackageVersion *DBRepository::findPackageVersionByMSIGUID_(
        const QString &guid) const
{
    PackageVersion* r = 0;

    QMySqlQuery q;
    q.prepare("SELECT ID, NAME, "
            "PACKAGE, CONTENT FROM PACKAGE_VERSION "
            "WHERE MSIGUID = :MSIGUID");
    q.bindValue(":MSIGUID", guid);
    q.exec();
    if (q.next()) {
        // TODO: handle error
        QDomDocument doc;
        int errorLine, errorColumn;
        QString err;
        if (!doc.setContent(q.value(3).toByteArray(),
                &err, &errorLine, &errorColumn))
            err = QString(
                    "XML parsing failed at line %1, column %2: %3").
                    arg(errorLine).arg(errorColumn).arg(err);

        QDomElement root = doc.documentElement();
        PackageVersion* p = PackageVersion::parse(&root, &err);

        // TODO: handle this error
        if (err.isEmpty()) {
            r = p;
        }
    }

    return r;
}

QString DBRepository::clear()
{
    Job* job = new Job();

    if (job->shouldProceed("Starting an SQL transaction")) {
        QString err = exec("BEGIN TRANSACTION");
        if (!err.isEmpty())
            job->setErrorMessage(err);
        else
            job->setProgress(0.01);
    }

    if (job->shouldProceed("Clearing the packages table")) {
        QString err = exec("DELETE FROM PACKAGE");
        if (!err.isEmpty())
            job->setErrorMessage(err);
        else
            job->setProgress(0.1);
    }

    if (job->shouldProceed("Clearing the package versions table")) {
        QString err = exec("DELETE FROM PACKAGE_VERSION");
        if (!err.isEmpty())
            job->setErrorMessage(err);
        else
            job->setProgress(0.7);
    }

    if (job->shouldProceed("Clearing the licenses table")) {
        QString err = exec("DELETE FROM LICENSE");
        if (!err.isEmpty())
            job->setErrorMessage(err);
        else
            job->setProgress(0.96);
    }

    if (job->shouldProceed("Commiting the SQL transaction")) {
        QString err = exec("COMMIT");
        if (!err.isEmpty())
            job->setErrorMessage(err);
        else
            job->setProgress(1);
    }

    job->complete();

    return "";
}

void DBRepository::updateF5(Job* job)
{
    Repository* r = new Repository();
    if (job->shouldProceed("Clearing the database")) {
        // TODO: error is ignored
        clear();
        job->setProgress(0.1);
    }

    if (job->shouldProceed("Downloading the remote repositories")) {
        Job* sub = job->newSubJob(0.69);
        r->load(sub, false);
        if (!sub->getErrorMessage().isEmpty())
            job->setErrorMessage(sub->getErrorMessage());
        delete sub;

        /* TODO
        PackageVersion* pv = r->findOrCreatePackageVersion(
                "com.googlecode.windows-package-manager.Npackd",
                Version(WPMUtils::NPACKD_VERSION));
        if (!pv->installed()) {
            pv->setPath(WPMUtils::getExeDir());
        }
        */
    }

    if (job->shouldProceed("Filling the local database")) {
        Job* sub = job->newSubJob(0.09);
        insertAll(sub, r);
        if (!sub->getErrorMessage().isEmpty())
            job->setErrorMessage(sub->getErrorMessage());
        delete sub;
    }

    if (job->shouldProceed("Adding well-known packages")) {
        addWellKnownPackages();
        job->setProgress(0.9);
    }

    if (job->shouldProceed("Refreshing the installation status")) {
        Job* sub = job->newSubJob(0.1);
        InstalledPackages::getDefault()->refresh(sub);
        if (!sub->getErrorMessage().isEmpty())
            job->setErrorMessage(sub->getErrorMessage());
        delete sub;
    }

    delete r;
    job->complete();
}

void DBRepository::addWellKnownPackages()
{
    Package* p;

    p = this->findPackage_("com.microsoft.Windows");
    if (!p) {
        Package* p = new Package("com.microsoft.Windows", "Windows");
        p->url = "http://www.microsoft.com/windows/";
        p->description = "Operating system";

        // TODO: error message is ignored
        insertPackage(p);
    }
    delete p;

    p = this->findPackage_("com.microsoft.Windows32");
    if (!p) {
        Package* p = new Package("com.microsoft.Windows32", "Windows/32 bit");
        p->url = "http://www.microsoft.com/windows/";
        p->description = "Operating system";

        // TODO: error message is ignored
        insertPackage(p);
    }
    delete p;

    p = this->findPackage_("com.microsoft.Windows64");
    if (!p) {
        Package* p = new Package("com.microsoft.Windows64", "Windows/64 bit");
        p->url = "http://www.microsoft.com/windows/";
        p->description = "Operating system";

        // TODO: error message is ignored
        insertPackage(p);
    }
    delete p;

    p = findPackage_("com.googlecode.windows-package-manager.Npackd");
    if (!p) {
        Package* p = new Package("com.googlecode.windows-package-manager.Npackd",
                "Npackd");
        p->url = "http://code.google.com/p/windows-package-manager/";
        p->description = "package manager";

        // TODO: error message is ignored
        insertPackage(p);
    }
    delete p;

    p = this->findPackage_("com.oracle.JRE");
    if (!p) {
        Package* p = new Package("com.oracle.JRE", "JRE");
        p->url = "http://www.java.com/";
        p->description = "Java runtime";

        // TODO: error message is ignored
        insertPackage(p);
    }
    delete p;

    p = this->findPackage_("com.oracle.JRE64");
    if (!p) {
        Package* p = new Package("com.oracle.JRE64", "JRE/64 bit");
        p->url = "http://www.java.com/";
        p->description = "Java runtime";

        // TODO: error message is ignored
        insertPackage(p);
    }
    delete p;

    p = this->findPackage_("com.oracle.JDK");
    if (!p) {
        Package* p = new Package("com.oracle.JDK", "JDK");
        p->url = "http://www.oracle.com/technetwork/java/javase/overview/index.html";
        p->description = "Java development kit";

        // TODO: error message is ignored
        insertPackage(p);
    }
    delete p;

    p = this->findPackage_("com.oracle.JDK64");
    if (!p) {
        Package* p = new Package("com.oracle.JDK64", "JDK/64 bit");
        p->url = "http://www.oracle.com/technetwork/java/javase/overview/index.html";
        p->description = "Java development kit";

        // TODO: error message is ignored
        insertPackage(p);
    }
    delete p;

    p = this->findPackage_("com.microsoft.DotNetRedistributable");
    if (!p) {
        Package* p = new Package("com.microsoft.DotNetRedistributable",
                ".NET redistributable runtime");
        p->url = "http://msdn.microsoft.com/en-us/netframework/default.aspx";
        p->description = ".NET runtime";

        // TODO: error message is ignored
        insertPackage(p);
    }
    delete p;

    p = this->findPackage_("com.microsoft.WindowsInstaller");
    if (!p) {
        Package* p = new Package("com.microsoft.WindowsInstaller",
                "Windows Installer");
        p->url = "http://msdn.microsoft.com/en-us/library/cc185688(VS.85).aspx";
        p->description = "Package manager";

        // TODO: error message is ignored
        insertPackage(p);
    }
    delete p;

    p = this->findPackage_("com.microsoft.MSXML");
    if (!p) {
        Package* p = new Package("com.microsoft.MSXML",
                "Microsoft Core XML Services (MSXML)");
        p->url = "http://www.microsoft.com/downloads/en/details.aspx?FamilyID=993c0bcf-3bcf-4009-be21-27e85e1857b1#Overview";
        p->description = "XML library";

        // TODO: error message is ignored
        insertPackage(p);
    }
    delete p;
}

void DBRepository::insertAll(Job* job, Repository* r)
{
    if (job->shouldProceed("Starting an SQL transaction")) {
        QString err = exec("BEGIN TRANSACTION");
        if (!err.isEmpty())
            job->setErrorMessage(err);
        else
            job->setProgress(0.01);
    }

    if (job->shouldProceed("Inserting data in the packages table")) {
        QString err = insertPackages(r);
        if (err.isEmpty())
            job->setProgress(0.6);
        else
            job->setErrorMessage(err);
    }

    if (job->shouldProceed("Inserting data in the package versions table")) {
        QString err = insertPackageVersions(r);
        if (err.isEmpty())
            job->setProgress(0.95);
        else
            job->setErrorMessage(err);
    }

    if (job->shouldProceed("Inserting data in the licenses table")) {
        QString err = insertLicenses(r);
        if (err.isEmpty())
            job->setProgress(0.98);
        else
            job->setErrorMessage(err);
    }

    if (job->shouldProceed("Commiting the SQL transaction")) {
        QString err = exec("COMMIT");
        if (!err.isEmpty())
            job->setErrorMessage(err);
        else
            job->setProgress(1);
    }

    job->complete();
}

QString DBRepository::insertPackages(Repository* r)
{
    QString err;
    for (int i = 0; i < r->packages.count(); i++) {
        Package* p = r->packages.at(i);
        err = insertPackage(p);
        if (!err.isEmpty())
            break;
    }

    return err;
}

QString DBRepository::insertLicenses(Repository* r)
{
    QString err;
    for (int i = 0; i < r->licenses.count(); i++) {
        License* p = r->licenses.at(i);
        err = insertLicense(p);
        if (!err.isEmpty())
            break;
    }

    return err;
}

QString DBRepository::insertPackageVersions(Repository* r)
{
    QString err;
    for (int i = 0; i < r->packageVersions.count(); i++) {
        PackageVersion* p = r->packageVersions.at(i);
        err = insertPackageVersion(p);
        if (!err.isEmpty())
            break;
    }

    return err;
}

QString DBRepository::toString(const QSqlError& e)
{
    return e.type() == QSqlError::NoError ? "" : e.text();
}

QString DBRepository::open()
{
    QString err;

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
    QString path(QDir::home().path());
    path.append(QDir::separator()).append("Npackd.db");
    path = QDir::toNativeSeparators(path);
    db.setDatabaseName(path);
    db.open();
    err = toString(db.lastError());

    bool e = false;
    if (err.isEmpty()) {
        e = tableExists(&db, "PACKAGE", &err);
    }

    if (err.isEmpty()) {
        if (!e) {
            db.exec("CREATE TABLE PACKAGE(ID INTEGER PRIMARY KEY, NAME TEXT, "
                    "TITLE TEXT, "
                    "URL TEXT, "
                    "ICON TEXT, "
                    "DESCRIPTION TEXT, "
                    "LICENSE TEXT, "
                    "FULLTEXT TEXT"
                    ")");
            err = toString(db.lastError());
        }
    }

    if (err.isEmpty()) {
        if (!e) {
            db.exec("CREATE INDEX PACKAGE_FULLTEXT ON PACKAGE(FULLTEXT)");
            err = toString(db.lastError());
        }
    }

    if (err.isEmpty()) {
        e = tableExists(&db, "PACKAGE_VERSION", &err);
    }

    if (err.isEmpty()) {
        if (!e) {
            db.exec("CREATE TABLE PACKAGE_VERSION(ID INTEGER PRIMARY KEY, NAME TEXT, "
                    "PACKAGE TEXT, "
                    "CONTENT BLOB, MSIGUID TEXT)");
            err = toString(db.lastError());
        }
    }

    if (err.isEmpty()) {
        if (!e) {
            db.exec("CREATE INDEX PACKAGE_VERSION_PACKAGE ON PACKAGE_VERSION("
                    "PACKAGE)");
            err = toString(db.lastError());
        }
    }

    if (err.isEmpty()) {
        e = tableExists(&db, "LICENSE", &err);
    }

    if (err.isEmpty()) {
        if (!e) {
            db.exec("CREATE TABLE LICENSE(ID INTEGER PRIMARY KEY, NAME TEXT, "
                    "TITLE TEXT, "
                    "DESCRIPTION TEXT, "
                    "URL TEXT"
                    ")");
            err = toString(db.lastError());
        }
    }

    return err;
}

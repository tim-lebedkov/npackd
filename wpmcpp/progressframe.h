#ifndef PROGRESSFRAME_H
#define PROGRESSFRAME_H

#include <QFrame>

#include "job.h"

namespace Ui {
    class ProgressFrame;
}

class ProgressFrame : public QFrame
{
    Q_OBJECT
private:
    Job* job;
    time_t started, modified;
public:
    /**
     * @param parent parent widget
     * @param job a job reference (not freed here)
     * @param title dialog title
     */
    explicit ProgressFrame(QWidget *parent, Job* job, const QString& title);
    ~ProgressFrame();

private:
    Ui::ProgressFrame *ui;
private slots:
    void jobChanged(const JobState& s);
    void on_pushButtonCancel_clicked();
};

#endif // PROGRESSFRAME_H

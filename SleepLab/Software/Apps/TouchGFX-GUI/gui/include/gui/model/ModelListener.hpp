#ifndef MODELLISTENER_HPP
#define MODELLISTENER_HPP

#include <gui/model/Model.hpp>
#include <gui/common/FrontendApplication.hpp>

class ModelListener
{
public:
    ModelListener() : model(0) {}
    virtual ~ModelListener() {}

    void bind(Model *m) { model = m; }

    /// The service published a fresh report.
    virtual void onReportChanged(const Model::Report &r) { (void)r; }
    /// The service published a complete history burst.
    virtual void onHistoryChanged(const Model::History &h) { (void)h; }

protected:
    Model *model;
};

#endif // MODELLISTENER_HPP

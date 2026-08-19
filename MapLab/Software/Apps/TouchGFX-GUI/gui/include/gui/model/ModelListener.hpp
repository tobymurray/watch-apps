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

    /// The service published a fresh snapshot.
    virtual void onStatusChanged(const Model::Status &status) { (void)status; }

protected:
    Model *model;
};

#endif // MODELLISTENER_HPP

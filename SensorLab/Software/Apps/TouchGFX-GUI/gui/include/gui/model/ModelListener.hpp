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

    /// The service published a fresh snapshot -- a status, or a roster burst.
    virtual void onStateChanged(const Model::State &state) { (void)state; }

protected:
    Model *model;
};

#endif // MODELLISTENER_HPP

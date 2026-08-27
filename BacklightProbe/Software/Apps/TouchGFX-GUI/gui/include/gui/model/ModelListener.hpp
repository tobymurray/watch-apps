#ifndef MODELLISTENER_HPP
#define MODELLISTENER_HPP

#include <gui/model/Model.hpp>
#include <gui/common/FrontendApplication.hpp>

class ModelListener
{
public:
    ModelListener() : model(0) {}

    virtual ~ModelListener() {}

    void bind(Model* m)
    {
        model = m;
    }

    /**
     * @brief The service published a new snapshot.
     *
     * Fires on every snapshot, including ones that carry the same state as the
     * last: progress is continuous, so "unchanged" is not a reason to skip
     * the repaint of a byte counter.
     */
    virtual void onStatusChanged(const Model::Status& status) { (void)status; }

protected:
    Model* model;
};

#endif // MODELLISTENER_HPP

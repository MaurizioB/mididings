/*
 * mididings
 *
 * Copyright (C) 2008-2012  Dominic Sacré  <dominic.sacre@gmx.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "config.hh"
#include "engine.hh"
#include "patch.hh"
#include "midi_event.hh"
#include "units/base.hh"
#include "units/engine.hh"
#include "units/filters.hh"
#include "units/modifiers.hh"
#include "units/generators.hh"
#include "units/call.hh"
#include "curious_alloc.hh"

#include "util/python.hh"
#include "util/python_converters.hh"
#include "util/counted_objects.hh"

#include <boost/python/module.hpp>
#include <boost/python/def.hpp>
#include <boost/python/class.hpp>
#include <boost/python/scope.hpp>
#include <boost/python/operators.hpp>
#include <boost/python/call_method.hpp>
#include <boost/python/return_value_policy.hpp>
#include <boost/python/return_by_value.hpp>

#include <vector>
#include <string>
#include <cstdlib>

#ifdef ENABLE_DEBUG_STATS
#include <iostream>
#endif


namespace Mididings {


#ifdef ENABLE_DEBUG_STATS

void unload() {
    std::cout << "MidiEvent alloc: " << curious_alloc_base<MidiEvent>::max_utilization() << " " << curious_alloc_base<MidiEvent>::fallback_count() << std::endl;

    std::cout << "Engine: " << das::counted_objects<Engine>::allocated() << " " << das::counted_objects<Engine>::deallocated() << std::endl;
    std::cout << "Patch: " << das::counted_objects<Patch>::allocated() << " " << das::counted_objects<Patch>::deallocated() << std::endl;
    std::cout << "Patch::Module: " << das::counted_objects<Patch::Module>::allocated() << " " << das::counted_objects<Patch::Module>::deallocated() << std::endl;
    std::cout << "Units::Unit: " << das::counted_objects<Units::Unit>::allocated() << " " << das::counted_objects<Units::Unit>::deallocated() << std::endl;
    std::cout << "Units::UnitEx: " << das::counted_objects<Units::UnitEx>::allocated() << " " << das::counted_objects<Units::UnitEx>::deallocated() << std::endl;
    std::cout << "MidiEvent: " << das::counted_objects<MidiEvent>::allocated() << " " << das::counted_objects<MidiEvent>::deallocated() << std::endl;
}

#endif // ENABLE_DEBUG_STATS


class EngineWrap
  : public Engine
{
  public:
    EngineWrap(PyObject *self,
               std::string const & backend_name,
               std::string const & client_name,
               Backend::PortNameVector const & in_ports,
               Backend::PortNameVector const & out_ports,
               bool verbose)
      : Engine(backend_name, client_name, in_ports, out_ports, verbose)
      , _self(self)
    { }

    void scene_switch_callback(int scene, int subscene) {
        das::scoped_gil_lock gil;
        try {
            boost::python::call_method<void>(_self, "scene_switch_callback", scene, subscene);
        } catch (boost::python::error_already_set &) {
            PyErr_Print();
        }
    }

  private:
    PyObject *_self;
};



BOOST_PYTHON_MODULE(_mididings)
{
    namespace bp = boost::python;
    using bp::class_;
    using bp::bases;
    using bp::init;
    using bp::def;
    using boost::noncopyable;
    using namespace Units;

    PyEval_InitThreads();


    // list of supported backends
    def("available_backends", &Backend::available, bp::return_value_policy<bp::return_by_value>());

    // main engine class, derived from in python
    class_<Engine, EngineWrap, noncopyable>("Engine", init<std::string const &, std::string const &,
                                                       std::vector<std::string> const &, std::vector<std::string> const &, bool>())
        .def("connect_ports", &Engine::connect_ports)
        .def("add_scene", &Engine::add_scene)
        .def("set_processing", &Engine::set_processing)
        .def("start", &Engine::start)
        .def("switch_scene", &Engine::switch_scene)
        .def("current_scene", &Engine::current_scene)
        .def("current_subscene", &Engine::current_subscene)
        .def("output_event", &Engine::output_event)
        .def("time", &Engine::time)
#ifdef ENABLE_TEST
        .def("process", &Engine::process_test)
#endif // ENABLE_TEST
    ;

    // patch class, derived from in python
    {
    bp::scope patch_scope = class_<Patch, noncopyable>("Patch", init<Patch::ModulePtr>());

    class_<Patch::Module, noncopyable>("Module", bp::no_init);
    class_<Patch::Chain, bases<Patch::Module>, noncopyable>("Chain", init<Patch::ModuleVector>());
    class_<Patch::Fork, bases<Patch::Module>, noncopyable>("Fork", init<Patch::ModuleVector, bool>());
    class_<Patch::Single, bases<Patch::Module>, noncopyable>("Single", init<boost::shared_ptr<Unit> >());
    class_<Patch::Extended, bases<Patch::Module>, noncopyable>("Extended", init<boost::shared_ptr<UnitEx> >());
    }

    // midi event class, derived from in python
    class_<MidiEvent>("MidiEvent")
        .def_readwrite("type", &MidiEvent::type)
        .def_readwrite("port_", &MidiEvent::port)
        .def_readwrite("channel_", &MidiEvent::channel)
        .def_readwrite("data1", &MidiEvent::data1)
        .def_readwrite("data2", &MidiEvent::data2)
        .def("_get_sysex_data", &MidiEvent::get_sysex_data, bp::return_value_policy<bp::return_by_value>())
        .def("_set_sysex_data", &MidiEvent::set_sysex_data)
        .def(bp::self == bp::self)
        .def(bp::self != bp::self)
        .enable_pickling()
    ;


    // unit base classes
    class_<Unit, noncopyable>("Unit", bp::no_init);
    class_<UnitEx, noncopyable>("UnitEx", bp::no_init);
    class_<Filter, bases<Unit>, noncopyable>("Filter", bp::no_init);

    // base
    class_<Pass, bases<Unit>, noncopyable>("Pass", init<bool>());
    class_<TypeFilter, bases<Filter>, noncopyable>("TypeFilter", init<int>());
    class_<InvertedFilter, bases<Filter>, noncopyable>("InvertedFilter", init<boost::shared_ptr<Filter>, bool>());

    // filters
    class_<PortFilter, bases<Filter>, noncopyable>("PortFilter", init<std::vector<int> const &>());
    class_<ChannelFilter, bases<Filter>, noncopyable>("ChannelFilter", init<std::vector<int> const &>());
    class_<KeyFilter, bases<Filter>, noncopyable>("KeyFilter", init<int, int, std::vector<int> const &>());
    class_<VelocityFilter, bases<Filter>, noncopyable>("VelocityFilter", init<int, int>());
    class_<CtrlFilter, bases<Filter>, noncopyable>("CtrlFilter", init<std::vector<int> const &>());
    class_<CtrlValueFilter, bases<Filter>, noncopyable>("CtrlValueFilter", init<int, int>());
    class_<ProgramFilter, bases<Filter>, noncopyable>("ProgramFilter", init<std::vector<int> const &>());
    class_<SysExFilter, bases<Filter>, noncopyable>("SysExFilter", init<MidiEvent::SysExData const &, bool>());

    // modifiers
    class_<Port, bases<Unit>, noncopyable>("Port", init<int>());
    class_<Channel, bases<Unit>, noncopyable>("Channel", init<int>());
    class_<Transpose, bases<Unit>, noncopyable>("Transpose", init<int>());
    class_<Velocity, bases<Unit>, noncopyable>("Velocity", init<float, int>());
    class_<VelocitySlope, bases<Unit>, noncopyable>("VelocitySlope", init<std::vector<int> const &, std::vector<float> const &, int>());
    class_<CtrlMap, bases<Unit>, noncopyable>("CtrlMap", init<int, int>());
    class_<CtrlRange, bases<Unit>, noncopyable>("CtrlRange", init<int, int, int, int, int>());
    class_<CtrlCurve, bases<Unit>, noncopyable>("CtrlCurve", init<int, float, int>());
    class_<PitchbendRange, bases<Unit>, noncopyable>("PitchbendRange", init<int, int, int, int>());

    // generators
    class_<Generator, bases<Unit>, noncopyable>("Generator", init<int, int, int, int, int>());
    class_<SysExGenerator, bases<Unit>, noncopyable>("SysExGenerator", init<int, MidiEvent::SysExData const &>());

    // engine
    class_<Sanitize, bases<UnitEx>, noncopyable>("Sanitize", init<>());
    class_<SceneSwitch, bases<UnitEx>, noncopyable>("SceneSwitch", init<int, int>());
    class_<SubSceneSwitch, bases<UnitEx>, noncopyable>("SubSceneSwitch", init<int, int, bool>());

    // call
    class_<Call, bases<UnitEx>, noncopyable>("Call", init<bp::object, bool, bool>());


    // register to/from-python converters for various types
    das::register_vector_converters<int>();
    das::register_vector_converters<unsigned char>();
    das::register_vector_converters<float>();
    das::register_vector_converters<std::string>();
    das::register_vector_converters<MidiEvent>();
    das::register_vector_converters<Patch::ModulePtr>();

    das::register_map_converters<std::string, std::vector<std::string> >();

    das::register_enum_converters<MidiEventType>();


#ifdef ENABLE_DEBUG_STATS
    std::atexit(unload);
#endif
}


} // Mididings

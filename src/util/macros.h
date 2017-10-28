#pragma once


// Helper for defining simple properties with setters and getters that are
// passed by value using move assignment in the setter. The getter returns
// a const reference. The type must have a default constructor for proper
// initialization during construction and a move assignment operator for
// efficient passing and setting by value.
#define PROPERTY_SET_BYVAL_GET_BYREF(TYPE, NAME, CAP_NAME) \
public: void set##CAP_NAME(TYPE NAME) { m_##NAME = std::move(NAME); } \
public: TYPE const& get##CAP_NAME() const { return m_##NAME; } \
private: TYPE m_##NAME;

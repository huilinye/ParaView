r"""servermanager is a module for using paraview server manager in Python.
One can always use the server manager API directly. However, this module
provides an interface easier to use from Python by wrapping several VTK
classes around Python classes.

Note that, upon load, this module will create several sub-modules: sources,
filters and rendering. These modules can be used to instantiate specific
proxy types. For a list, try "dir(servermanager.sources)"

A simple example:
  from paraview.servermanager import *

  # Creates a new built-in connection and makes it the active connection.
  Connect()

  # Creates a new render view on the active connection.
  renModule = CreateRenderView()

  # Create a new sphere proxy on the active connection and register it
  # in the sources group.
  sphere = sources.SphereSource(registrationGroup="sources", ThetaResolution=16, PhiResolution=32)

  # Create a representation for the sphere proxy and adds it to the render
  # module.
  display = CreateRepresentation(sphere, renModule)

  renModule.StillRender()
"""
#==============================================================================
#
#  Program:   ParaView
#  Module:    $RCSfile$
#
#  Copyright (c) Kitware, Inc.
#  All rights reserved.
#  See Copyright.txt or http://www.paraview.org/HTML/Copyright.html for details.
#
#     This software is distributed WITHOUT ANY WARRANTY without even
#     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
#     PURPOSE.  See the above copyright notice for more information.
#
#==============================================================================
import paraview, re, os, new, sys, vtk

if not paraview.compatibility.minor:
    paraview.compatibility.major = 3
if not paraview.compatibility.major:
    paraview.compatibility.minor = 5

if os.name == "posix":
    from libvtkPVServerCommonPython import *
    from libvtkPVServerManagerPython import *
else:
    from vtkPVServerCommonPython import *
    from vtkPVServerManagerPython import *

def _wrap_property(proxy, smproperty):
    """ Internal function.
    Given a server manager property and its domains, returns the
    appropriate python object.
    """
    property = None
    if paraview.compatibility.GetVersion() >= 3.5 and \
      smproperty.IsA("vtkSMStringVectorProperty"):
        al = smproperty.GetDomain("array_list")
        if  al and al.IsA("vtkSMArraySelectionDomain") and \
            smproperty.GetRepeatable():
            property = ArrayListProperty(proxy, smproperty)
        elif al and al.IsA("vtkSMArrayListDomain") and smproperty.GetNumberOfElements() == 5:
            property = ArraySelectionProperty(proxy, smproperty)
        else:
            iter = smproperty.NewDomainIterator()
            isFileName = False
            while not iter.IsAtEnd():
                if iter.GetDomain().IsA("vtkSMFileListDomain"):
                    isFileName = True
                    break
                iter.Next()
            iter.UnRegister(None)
            if isFileName:
                property = FileNameProperty(proxy, smproperty)
            elif _make_name_valid(smproperty.GetXMLLabel()) == 'ColorArrayName':
                property = ColorArrayProperty(proxy, smproperty)
            else:
                property = VectorProperty(proxy, smproperty)
    elif smproperty.IsA("vtkSMVectorProperty"):
        if smproperty.IsA("vtkSMIntVectorProperty") and \
          smproperty.GetDomain("enum"):
            property = EnumerationProperty(proxy, smproperty)
        else:
            property = VectorProperty(proxy, smproperty)
    elif smproperty.IsA("vtkSMInputProperty"):
        property = InputProperty(proxy, smproperty)
    elif smproperty.IsA("vtkSMProxyProperty"):
        property = ProxyProperty(proxy, smproperty)
    else:
        property = Property(proxy, smproperty)
    return property

class Proxy(object):
    """Proxy for a server side object. A proxy manages the lifetime of
    one or more server manager objects. It also provides an interface
    to set and get the properties of the server side objects. These
    properties are presented as Python properties. For example,
    you can set a property Foo using the following:
     proxy.Foo = (1,2)
    or
     proxy.Foo.SetData((1,2))
    or
     proxy.Foo[0:2] = (1,2)
    For more information, see the documentation of the property which
    you can obtain with
    help(proxy.Foo).

    This class also provides an iterator which can be used to iterate
    over all properties.
    eg:
      proxy = Proxy(proxy=smproxy)
      for property in proxy:
          print property

    For advanced users:
    This is a python class that wraps a vtkSMProxy.. Makes it easier to
    set/get properties.
    Instead of:
     proxy.GetProperty("Foo").SetElement(0, 1)
     proxy.GetProperty("Foo").SetElement(0, 2)
    you can do:
     proxy.Foo = (1,2)
    or
     proxy.Foo.SetData((1,2))
    or
     proxy.Foo[0:2] = (1,2)
    Instead of:
      proxy.GetProperty("Foo").GetElement(0)
    you can do:
      proxy.Foo.GetData()[0]
    or
      proxy.Foo[0]
    For proxy properties, you can use append:
     proxy.GetProperty("Bar").AddProxy(foo)
    you can do:
     proxy.Bar.append(foo)
    Properties support most of the list API. See VectorProperty and
    ProxyProperty documentation for details.

    Please note that some of the methods accessible through the Proxy
    class are not listed by help() because the Proxy objects forward
    unresolved attributes to the underlying object. To get the full list,
    see also dir(proxy.SMProxy). See also the doxygen based documentation
    of the vtkSMProxy C++ class.
    """

    def __init__(self, **args):
        """ Default constructor. It can be used to initialize properties
        by passing keyword arguments where the key is the name of the
        property. In addition registrationGroup and registrationName (optional)
        can be specified (as keyword arguments) to automatically register
        the proxy with the proxy manager. """
        self.add_attribute('Observed', None)
        self.add_attribute('ObserverTag', -1)
        self.add_attribute('_Proxy__Properties', {})
        self.add_attribute('_Proxy__LastAttrName', None)
        self.add_attribute('SMProxy', None)
        self.add_attribute('Port', 0)

        if 'port' in args:
            self.Port = args['port']
            del args['port']

        if 'proxy' in args:
            self.InitializeFromProxy(args['proxy'])
            del args['proxy']
        else:
            self.Initialize()
        if 'registrationGroup' in args:
            registrationGroup = args['registrationGroup']
            del args['registrationGroup']
            registrationName = self.SMProxy.GetSelfIDAsString()
            if 'registrationName' in args:
                registrationName = args['registrationName']
                del args['registrationName']
            pxm = ProxyManager()
            pxm.RegisterProxy(registrationGroup, registrationName, self.SMProxy)
        for key in args.keys():
            setattr(self, key, args[key])
        self.UpdateVTKObjects()
        # Visit all properties so that they are created
        for prop in self:
            pass

    def __setattr__(self, name, value):
        try:
            setter = getattr(self.__class__, name)
            setter = setter.__set__
        except AttributeError:
            if not hasattr(self, name):
                raise AttributeError("Attribute %s does not exist. " % name +
                  " This class does not allow addition of new attributes to avoid " +
                  "mistakes due to typos. Use add_attribute() if you really want " +
                  "to add this attribute.")
            self.__dict__[name] = value
        else:
            setter(self, value)

    def add_attribute(self, name, value):
        self.__dict__[name] = value

    def __del__(self):
        """Destructor. Cleans up all observers as well as remove
        the proxy from the _pyproxies dictionary"""
        # Make sure that we remove observers we added
        if self.Observed:
            observed = self.Observed
            tag = self.ObserverTag
            self.Observed = None
            self.ObserverTag = -1
            observed.RemoveObserver(tag)
        if self.SMProxy and (self.SMProxy, self.Port) in _pyproxies:
            del _pyproxies[(self.SMProxy, self.Port)]

    def InitializeFromProxy(self, aProxy):
        """Constructor. Assigns proxy to self.SMProxy, updates the server
        object as well as register the proxy in _pyproxies dictionary."""
        import weakref
        self.SMProxy = aProxy
        self.SMProxy.UpdateVTKObjects()
        _pyproxies[(self.SMProxy, self.Port)] = weakref.ref(self)

    def Initialize(self):
        "Overridden by the subclass created automatically"
        pass

    def __eq__(self, other):
        "Returns true if the underlying SMProxies are the same."
        if isinstance(other, Proxy):
            return self.SMProxy == other.SMProxy
        return self.SMProxy == other

    def __ne__(self, other):
        "Returns false if the underlying SMProxies are the same."
        return not self.__eq__(other)

    def __iter__(self):
        "Creates an iterator for the properties."
        return PropertyIterator(self)

    def SetPropertyWithName(self, pname, arg):
        """Generic method for setting the value of a property."""
        prop = self.GetProperty(pname)
        if prop is None:
            raise RuntimeError, "Property %s does not exist. Please check the property name for typos." % pname
        prop.SetData(arg)

    def GetPropertyValue(self, name):
        """Returns a scalar for properties with 1 elements, the property
        itself for vectors."""
        p = self.GetProperty(name)
        if isinstance(p, VectorProperty):
            if p.GetNumberOfElements() == 1 and not p.GetRepeatable():
                if p.SMProperty.IsA("vtkSMStringVectorProperty") or not p.GetArgumentIsArray():
                    return p[0]
        elif isinstance(p, InputProperty):
            if not p.GetMultipleInput():
                if len(p) > 0:
                    return p[0]
                else:
                    return None
        elif isinstance(p, ProxyProperty):
            if not p.GetRepeatable():
                if len(p) > 0:
                    return p[0]
                else:
                    return None
        return p

    def GetProperty(self, name):
        """Given a property name, returns the property object."""
        if name in self.__Properties and self.__Properties[name]():
            return self.__Properties[name]()
        smproperty = self.SMProxy.GetProperty(name)
        # Maybe they are looking by the label. Try to match that.
        if not smproperty:
            iter = PropertyIterator(self)
            for prop in iter:
                if name == _make_name_valid(iter.PropertyLabel):
                    smproperty = prop.SMProperty
                    break
        if smproperty:
            property = _wrap_property(self, smproperty)
            if property is not None:
                import weakref
                self.__Properties[name] = weakref.ref(property)
            return property
        return None

    def ListProperties(self):
        """Returns a list of all property names on this proxy."""
        property_list = []
        iter = self.__iter__()
        for property in iter:
            name = _make_name_valid(iter.PropertyLabel)
            if name:
                property_list.append(name)
        return property_list

    def __ConvertArgumentsAndCall(self, *args):
        """ Internal function.
        Used to call a function on SMProxy. Converts input and
        output values as appropriate.
        """
        newArgs = []
        for arg in args:
            if issubclass(type(arg), Proxy) or isinstance(arg, Proxy):
                newArgs.append(arg.SMProxy)
            else:
                newArgs.append(arg)
        func = getattr(self.SMProxy, self.__LastAttrName)
        retVal = func(*newArgs)
        if type(retVal) is type(self.SMProxy) and retVal.IsA("vtkSMProxy"):
            return _getPyProxy(retVal)
        elif type(retVal) is type(self.SMProxy) and retVal.IsA("vtkSMProperty"):
            return _wrap_property(self, retVal)
        else:
            return retVal

    def __GetActiveCamera(self):
        """ This method handles GetActiveCamera specially. It adds
        an observer to the camera such that everytime it is modified
        the render view updated"""
        import weakref
        c = self.SMProxy.GetActiveCamera()
        if not c.HasObserver("ModifiedEvent"):
            self.ObserverTag =c.AddObserver("ModifiedEvent", \
                              _makeUpdateCameraMethod(weakref.ref(self)))
            self.Observed = c
        return c

    def __getattr__(self, name):
        """With the exception of a few overloaded methods,
        returns the SMProxy method"""
        if not self.SMProxy:
            raise AttributeError("class has no attribute %s" % name)
            return None
        # Handle GetActiveCamera specially.
        if name == "GetActiveCamera" and \
           hasattr(self.SMProxy, "GetActiveCamera"):
            return self.__GetActiveCamera
        if name == "SaveDefinition" and hasattr(self.SMProxy, "SaveDefinition"):
            return self.__SaveDefinition
        # If not a property, see if SMProxy has the method
        try:
            proxyAttr = getattr(self.SMProxy, name)
            self.__LastAttrName = name
            return self.__ConvertArgumentsAndCall
        except:
            pass
        return getattr(self.SMProxy, name)

class SourceProxy(Proxy):
    """Proxy for a source object. This class adds a few methods to Proxy
    that are specific to sources. It also provides access to the output
    ports. Output ports can be accessed by name or index:
    > op = source[0]
    or
    > op = source['some name'].
    """
    def UpdatePipeline(self, time=None):
        """This method updates the server-side VTK pipeline and the associated
        data information. Make sure to update a source to validate the output
        meta-data."""
        if time:
            self.SMProxy.UpdatePipeline(time)
        else:
            self.SMProxy.UpdatePipeline()
        # Fetch the new information. This is also here to cause a receive
        # on the client side so that progress works properly.
        self.SMProxy.GetDataInformation()

    def FileNameChanged(self):
        "Called when the filename of a source proxy is changed."
        self.UpdatePipelineInformation()

    def UpdatePipelineInformation(self):
        """This method updates the meta-data of the server-side VTK pipeline and
        the associated information properties"""
        self.SMProxy.UpdatePipelineInformation()

    def GetDataInformation(self, idx=None):
        """This method returns a DataInformation wrapper around a
        vtkPVDataInformation"""
        if not idx:
            idx = self.Port
        if self.SMProxy:
            return DataInformation( \
                self.SMProxy.GetDataInformation(idx), \
                self.SMProxy, idx)

    def __getitem__(self, idx):
        """Given a slice, int or string, returns the corresponding
        output port"""
        if isinstance(idx, slice):
            indices = idx.indices(self.SMProxy.GetNumberOfOutputPorts())
            retVal = []
            for i in range(*indices):
                retVal.append(OutputPort(self, i))
            return retVal
        elif isinstance(idx, int):
            if idx >= self.SMProxy.GetNumberOfOutputPorts() or idx < 0:
                raise IndexError
            return OutputPort(self, idx)
        else:
            return OutputPort(self, self.SMProxy.GetOutputPortIndex(idx))

    def GetPointDataInformation(self):
        """Returns the associated point data information."""
        self.UpdatePipeline()
        return FieldDataInformation(self.SMProxy, self.Port, "PointData")

    def GetCellDataInformation(self):
        """Returns the associated cell data information."""
        self.UpdatePipeline()
        return FieldDataInformation(self.SMProxy, self.Port, "CellData")

    def GetFieldDataInformation(self):
        """Returns the associated cell data information."""
        self.UpdatePipeline()
        return FieldDataInformation(self.SMProxy, self.Port, "FieldData")

    PointData = property(GetPointDataInformation, None, None, "Returns point data information")
    CellData = property(GetCellDataInformation, None, None, "Returns cell data information")
    FieldData = property(GetFieldDataInformation, None, None, "Returns field data information")


class ExodusIIReaderProxy(SourceProxy):
    """Special class to define convenience functions for array
    selection."""

    if paraview.compatibility.GetVersion() >= 3.5:
        def FileNameChanged(self):
            "Called when the filename changes. Selects all variables."
            SourceProxy.FileNameChanged(self)
            self.SelectAllVariables()

        def SelectAllVariables(self):
            "Select all available variables for reading."
            for prop in ('PointVariables', 'EdgeVariables', 'FaceVariables',
                'ElementVariables', 'GlobalVariables'):
                f = getattr(self, prop)
                f.SelectAll()

        def DeselectAllVariables(self):
            "Deselects all variables."
            for prop in ('PointVariables', 'EdgeVariables', 'FaceVariables',
                'ElementVariables', 'GlobalVariables'):
                f = getattr(self, prop)
                f.DeselectAll()

class Property(object):
    """Generic property object that provides access to one of the properties of
    a server object. This class does not allow setting/getting any values but
    provides an interface to update a property using __call__. This can be used
    for command properties that correspond to function calls without arguments.
    For example,
    > proxy.Foo()
    would push a Foo property which may cause the proxy to call a Foo method
    on the actual VTK object.

    For advanced users:
    Python wrapper around a vtkSMProperty with a simple interface.
    In addition to all method provided by vtkSMProperty (obtained by
    forwarding unknown attributes requests to the underlying SMProxy),
    Property and sub-class provide a list API.

    Please note that some of the methods accessible through the Property
    class are not listed by help() because the Property objects forward
    unresolved attributes to the underlying object. To get the full list,
    see also dir(proxy.SMProperty). See also the doxygen based documentation
    of the vtkSMProperty C++ class.
    """
    def __init__(self, proxy, smproperty):
        """Default constructor. Stores a reference to the proxy."""
        import weakref
        self.SMProperty = smproperty
        self.Proxy = proxy

    def __repr__(self):
        """Returns a string representation containing property name
        and value"""
        if not type(self) is Property:
            if self.GetData() is not None:
                repr = self.GetData().__repr__()
            else:
                repr = "None"
        else:
            repr = "Property name= "
            name = self.Proxy.GetPropertyName(self.SMProperty)
            if name:
                repr += name
            else:
                repr += "Unknown"

        return repr

    def __call__(self):
        """Forces a property update using InvokeCommand."""
        if type(self) is Property:
            self.Proxy.SMProxy.InvokeCommand(self._FindPropertyName())
        else:
            raise RuntimeError, "Cannot invoke this property"

    def _FindPropertyName(self):
        "Returns the name of this property."
        return self.Proxy.GetPropertyName(self.SMProperty)

    def _UpdateProperty(self):
        "Pushes the value of this property to the server."
        # For now, we are updating all properties. This is due to an
        # issue with the representations. Their VTK objects are not
        # created until Input is set therefore, updating a property
        # has no effect. Updating all properties everytime one is
        # updated has the effect of pushing values set before Input
        # when Input is updated.
        # self.Proxy.SMProxy.UpdateProperty(self._FindPropertyName())
        self.Proxy.SMProxy.UpdateVTKObjects()

    def __getattr__(self, name):
        "Unknown attribute requests get forwarded to SMProperty."
        return getattr(self.SMProperty, name)

class GenericIterator(object):
    """Iterator for container type objects"""

    def __init__(self, obj):
        self.Object = obj
        self.index = 0

    def __iter__(self):
        return self

    def next(self):
        if self.index >= len(self.Object):
            raise StopIteration

        idx = self.index
        self.index += 1
        return self.Object[idx]

class VectorProperty(Property):
    """A VectorProperty provides access to one or more values. You can use
    a slice to get one or more property values:
    > val = property[2]
    or
    > vals = property[0:5:2]
    You can use a slice to set one or more property values:
    > property[2] = val
    or
    > property[1:3] = (1,2)
    """
    def ConvertValue(self, value):
        return value

    def __len__(self):
        """Returns the number of elements."""
        return self.SMProperty.GetNumberOfElements()

    def __iter__(self):
        """Implementation of the sequence API"""
        return GenericIterator(self)

    def __setitem__(self, idx, value):
        """Given a list or tuple of values, sets a slice of values [min, max)"""
        if isinstance(idx, slice):
            indices = idx.indices(len(self))
            for i, j in zip(range(*indices), value):
                self.SMProperty.SetElement(i, self.ConvertValue(j))
            self._UpdateProperty()
        elif idx >= len(self) or idx < 0:
            raise IndexError
        else:
            self.SMProperty.SetElement(idx, self.ConvertValue(value))
            self._UpdateProperty()

    def GetElement(self, index):
        return self.SMProperty.GetElement(index)

    def __getitem__(self, idx):
        """Returns the range [min, max) of elements. Raises an IndexError
        exception if an argument is out of bounds."""
        ls = len(self)
        if isinstance(idx, slice):
            indices = idx.indices(ls)
            retVal = []
            for i in range(*indices):
               retVal.append(self.GetElement(i))
            return retVal
        elif idx >= ls:
            raise IndexError
        elif idx < 0:
            idx = ls + idx
            if idx < 0:
                raise IndexError

        return self.GetElement(idx)

    def GetData(self):
        "Returns all elements as either a list or a single value."
        property = self.SMProperty
        if property.GetRepeatable() or \
           property.GetNumberOfElements() > 1:
            return self[0:len(self)]
        elif property.GetNumberOfElements() == 1:
            return self.GetElement(0)

    def SetData(self, values):
        """Allows setting of all values at once. Requires a single value or
        a iterable object."""
        if not hasattr(values, "__iter__"):
            values = (values,)
        iup = self.SMProperty.GetImmediateUpdate()
        self.SMProperty.SetImmediateUpdate(False)
        if not self.GetRepeatable() and len(values) != self.GetNumberOfElements():
            raise RuntimeError("This property requires %d values." % self.GetNumberOfElements())
        if self.GetRepeatable():
            # Clean up first
            self.SMProperty.SetNumberOfElements(0)
        idx = 0
        for val in values:
            self.SMProperty.SetElement(idx, self.ConvertValue(val))
            idx += 1
        self.SMProperty.SetImmediateUpdate(iup)
        self._UpdateProperty()

    def Clear(self):
        "Removes all elements."
        self.SMProperty().SetNumberOfElements(0)
        self._UpdateProperty()

class ColorArrayProperty(VectorProperty):
    """This subclass of VectorProperty handles setting of the array to
    color by. It handles attribute type as well as well array name."""

    def GetAvailable(self):
        """"Returns the list of available arrays as (attribute type, array name
        tuples."""
        arrays = []
        for a in self.Proxy.Input.PointData:
            arrays.append(('POINT_DATA', a.GetName()))
        for a in self.Proxy.Input.CellData:
            arrays.append(('CELL_DATA', a.GetName()))
        return arrays

    def SetData(self, value):
        """Overwritten to enable setting attribute type (the ColorAttributeType
        property and the array name. The argument should be the array name
        (in which case the first appropriate attribute type is picked) or
        a tuple of attribute type and array name."""
        if isinstance(value, tuple) and len(value) == 2:
            att = value[0]
            arr = value[1]
        elif isinstance(value, str):
            att = None
            arr = value
        else:
            raise ValueError("Expected a tuple of 2 values or a string.")

        if not arr:
            self.SMProperty.SetElement(0, '')
            self._UpdateProperty()
            return

        found = False
        for a in self.Available:
            if a[1] == arr and (not att or att == a[0]):
                att = a[0]
                found = True
                break

        if  not found:
            raise ValueError("Could not locate array %s in the input." % arr)

        catt = self.Proxy.GetProperty("ColorAttributeType")
        catt.SetData(att)
        self.SMProperty.SetElement(0, arr)
        self._UpdateProperty()

    Available = property(GetAvailable, None, None, \
        "This read-only property returns the list of arrays that can be colored by.")


class EnumerationProperty(VectorProperty):
    """"Subclass of VectorProperty that is applicable for enumeration type
    properties."""

    def GetElement(self, index):
        """Returns the text for the given element if available. Returns
        the numerical values otherwise."""
        val = self.SMProperty.GetElement(index)
        domain = self.SMProperty.GetDomain("enum")
        for i in range(domain.GetNumberOfEntries()):
            if domain.GetEntryValue(i) == val:
                return domain.GetEntryText(i)
        return val

    def ConvertValue(self, value):
        """Converts value to type suitable for vtSMProperty::SetElement()"""
        if type(value) == str:
            domain = self.SMProperty.GetDomain("enum")
            if domain.HasEntryText(value):
                return domain.GetEntryValueForText(value)
            else:
                raise ValueError("%s is not a valid value." % value)
        return VectorProperty.ConvertValue(self, value)

    def GetAvailable(self):
        "Returns the list of available values for the property."
        retVal = []
        domain = self.SMProperty.GetDomain("enum")
        for i in range(domain.GetNumberOfEntries()):
            retVal.append(domain.GetEntryText(i))
        return retVal

    Available = property(GetAvailable, None, None, \
        "This read-only property contains the list of values that can be applied to this property.")


class FileNameProperty(VectorProperty):
    """Property to set/get one or more file names.
    This property updates the pipeline information everytime its value changes.
    This is used to keep the array lists up to date."""

    def _UpdateProperty(self):
        "Pushes the value of this property to the server."
        VectorProperty._UpdateProperty(self)
        self.Proxy.FileNameChanged()

class ArraySelectionProperty(VectorProperty):
    "Property to select an array to be processed by a filter."

    def GetAssociation(self):
        val = self.GetElement(3)
        if val == "":
            return None
        for key, value in ASSOCIATIONS.iteritems():
            if value == int(val):
                return key

        return None

    def GetArrayName(self):
        return self.GetElement(4)

    def __len__(self):
        """Returns the number of elements."""
        return 2

    def __setitem__(self, idx, value):
        raise RuntimeError, "This property cannot be accessed using __setitem__"

    def __getitem__(self, idx):
        """Returns attribute type for index 0, array name for index 1"""
        if isinstance(idx, slice):
            indices = idx.indices(len(self))
            retVal = []
            for i in range(*indices):
                if i >= 2 or i < 0:
                    raise IndexError
                if i == 0:
                    retVal.append(self.GetAssociation())
                else:
                    retVal.append(self.GetArrayName())
            return retVal
        elif idx >= 2 or idx < 0:
            raise IndexError

        if i == 0:
            return self.GetAssociation()
        else:
            return self.GetArrayName()

    def SetData(self, values):
        """Allows setting of all values at once. Requires a single value,
        a tuple or list."""
        if not isinstance(values, tuple) and \
           not isinstance(values, list):
            values = (values,)
        if len(values) == 1:
            self.SMProperty.SetElement(4, values[0])
        elif len(values) == 2:
            if isinstance(values[0], str):
                val = str(ASSOCIATIONS[values[0]])
            self.SMProperty.SetElement(3,  str(val))
            self.SMProperty.SetElement(4, values[1])
        else:
            raise RuntimeError, "Expected 1 or 2 values."
        self._UpdateProperty()

    def UpdateDefault(self):
        "Helper method to set default values."
        if self.SMProperty.GetNumberOfElements() != 5:
            return
        if self.GetElement(4) != '' or \
            self.GetElement(3) != '':
            return

        for i in range(0,3):
            if self.GetElement(i) == '':
                self.SMProperty.SetElement(i, '0')
        al = self.SMProperty.GetDomain("array_list")
        al.Update(self.SMProperty)
        al.SetDefaultValues(self.SMProperty)

class ArrayListProperty(VectorProperty):
    """This property provides a simpler interface for selecting arrays.
    Simply assign a list of arrays that should be loaded by the reader.
    Use the Available property to get a list of available arrays."""

    def __init__(self, proxy, smproperty):
        VectorProperty.__init__(self, proxy, smproperty)
        self.__arrays = []

    def GetAvailable(self):
        "Returns the list of available arrays"
        dm = self.GetDomain("array_list")
        retVal = []
        for i in range(dm.GetNumberOfStrings()):
            retVal.append(dm.GetString(i))
        return retVal

    Available = property(GetAvailable, None, None, \
        "This read-only property contains the list of items that can be read by a reader.")

    def SelectAll(self):
        "Selects all arrays."
        self.SetData(self.Available)

    def DeselectAll(self):
        "Deselects all arrays."
        self.SetData([])

    def __iter__(self):
        """Implementation of the sequence API"""
        return GenericIterator(self)

    def __len__(self):
        """Returns the number of elements."""
        return len(self.GetData())

    def __setitem__(self, idx, value):
      """Given a list or tuple of values, sets a slice of values [min, max)"""
      self.GetData()
      if isinstance(idx, slice):
          indices = idx.indices(len(self))
          for i, j in zip(range(*indices), value):
              self.__arrays[i] = j
          self.SetData(self.__arrays)
      elif idx >= len(self) or idx < 0:
          raise IndexError
      else:
          self.__arrays[idx] = self.ConvertValue(value)
          self.SetData(self.__arrays)

    def __getitem__(self, idx):
      """Returns the range [min, max) of elements. Raises an IndexError
      exception if an argument is out of bounds."""
      self.GetData()
      if isinstance(idx, slice):
          indices = idx.indices(len(self))
          retVal = []
          for i in range(*indices):
              retVal.append(self.__arrays[i])
          return retVal
      elif idx >= len(self) or idx < 0:
          raise IndexError
      return self.__arrays[idx]

    def SetData(self, values):
        """Allows setting of all values at once. Requires a single value,
        a tuple or list."""
        # Clean up first
        iup = self.SMProperty.GetImmediateUpdate()
        self.SMProperty.SetImmediateUpdate(False)
        # Clean up first
        self.SMProperty.SetNumberOfElements(0)
        if not isinstance(values, tuple) and \
           not isinstance(values, list):
            values = (values,)
        fullvalues = []
        for i in range(len(values)):
            val = self.ConvertValue(values[i])
            fullvalues.append(val)
            fullvalues.append('1')
        for array in self.Available:
            if not values.__contains__(array):
                fullvalues.append(array)
                fullvalues.append('0')
        i = 0
        for value in fullvalues:
            self.SMProperty.SetElement(i, value)
            i += 1

        self._UpdateProperty()
        self.SMProperty.SetImmediateUpdate(iup)

    def GetData(self):
        "Returns all elements as a list."
        property = self.SMProperty
        nElems = property.GetNumberOfElements()
        if nElems%2 != 0:
            raise ValueError, "The SMProperty with XML label '%s' has a size that is not a multiple of 2." % property.GetXMLLabel()
        self.__arrays = []
        for i in range(0, nElems, 2):
            if self.GetElement(i+1) != '0':
                self.__arrays.append(self.GetElement(i))
        return list(self.__arrays)

class ProxyProperty(Property):
    """A ProxyProperty provides access to one or more proxies. You can use
    a slice to get one or more property values:
    > proxy = property[2]
    or
    > proxies = property[0:5:2]
    You can use a slice to set one or more property values:
    > property[2] = proxy
    or
    > property[1:3] = (proxy1, proxy2)
    You can also append and delete:
    > property.append(proxy)
    and
    > del property[1:2]

    You can also remove all elements with Clear().

    Note that some properties expect only 1 proxy and will complain if
    you set the number of values to be something else.
    """
    def __init__(self, proxy, smproperty):
        """Default constructor.  Stores a reference to the proxy.  Also looks
        at domains to find valid values."""
        Property.__init__(self, proxy, smproperty)
        # Check to see if there is a proxy list domain and, if so,
        # initialize ourself. (Should this go in ProxyProperty?)
        listdomain = self.GetDomain('proxy_list')
        if listdomain:
            if listdomain.GetClassName() != 'vtkSMProxyListDomain':
                raise ValueError, "Found a 'proxy_list' domain on an InputProperty that is not a ProxyListDomain."
            pm = ProxyManager()
            group = "pq_helper_proxies." + proxy.GetSelfIDAsString()
            if listdomain.GetNumberOfProxies() == 0:
                for i in xrange(listdomain.GetNumberOfProxyTypes()):
                    igroup = listdomain.GetProxyGroup(i)
                    name = listdomain.GetProxyName(i)
                    iproxy = CreateProxy(igroup, name)
                    listdomain.AddProxy(iproxy)
                    pm.RegisterProxy(group, proxy.GetPropertyName(smproperty), iproxy)
                listdomain.SetDefaultValues(self.SMProperty)

    def GetAvailable(self):
        """If this proxy has a list domain, then this function returns the
        strings you can use to select from the domain.  If there is no such
        list domain, the returned list is empty."""
        listdomain = self.GetDomain('proxy_list')
        retval = []
        if listdomain:
            for i in xrange(listdomain.GetNumberOfProxies()):
                proxy = listdomain.GetProxy(i)
                retval.append(proxy.GetXMLLabel())
        return retval

    Available = property(GetAvailable, None, None,
                         """This read only property is a list of strings you can
                         use to select from the list domain.  If there is no
                         such list domain, the array is empty.""")

    def __iter__(self):
        """Implementation of the sequence API"""
        return GenericIterator(self)

    def __len__(self):
        """Returns the number of elements."""
        return self.SMProperty.GetNumberOfProxies()

    def remove(self, proxy):
        """Removes the first occurence of the proxy from the property."""
        self.SMProperty.RemoveProxy(proxy.SMProxy)
        self._UpdateProperty()
        
    def __setitem__(self, idx, value):
      """Given a list or tuple of values, sets a slice of values [min, max)"""
      if isinstance(idx, slice):
        indices = idx.indices(len(self))
        for i, j in zip(range(*indices), value):
          self.SMProperty.SetProxy(i, j.SMProxy)
        self._UpdateProperty()
      elif idx >= len(self) or idx < 0:
        raise IndexError
      else:
        self.SMProperty.SetProxy(idx, value.SMProxy)
        self._UpdateProperty()

    def __delitem__(self,idx):
      """Removes the element idx"""
      if isinstance(idx, slice):
        indices = idx.indices(len(self))
        # Collect the elements to delete to a new list first.
        # Otherwise indices are screwed up during the actual
        # remove loop.
        toremove = []
        for i in range(*indices):
          toremove.append(self[i])
        for i in toremove:
          self.SMProperty.RemoveProxy(i.SMProxy)
        self._UpdateProperty()
      elif idx >= len(self) or idx < 0:
        raise IndexError
      else:
        self.SMProperty.RemoveProxy(self[idx].SMProxy)
        self._UpdateProperty()

    def __getitem__(self, idx):
      """Returns the range [min, max) of elements. Raises an IndexError
      exception if an argument is out of bounds."""
      if isinstance(idx, slice):
        indices = idx.indices(len(self))
        retVal = []
        for i in range(*indices):
          retVal.append(_getPyProxy(self.SMProperty.GetProxy(i)))
        return retVal
      elif idx >= len(self) or idx < 0:
        raise IndexError
      return _getPyProxy(self.SMProperty.GetProxy(idx))

    def __getattr__(self, name):
        "Unknown attribute requests get forwarded to SMProperty."
        return getattr(self.SMProperty, name)

    def index(self, proxy):
        idx = 0
        for px in self:
            if proxy == px:
                return idx
            idx += 1
        raise ValueError("proxy is not in the list.")

    def append(self, proxy):
        "Appends the given proxy to the property values."
        self.SMProperty.AddProxy(proxy.SMProxy)
        self._UpdateProperty()

    def GetData(self):
        "Returns all elements as either a list or a single value."
        property = self.SMProperty
        if property.GetRepeatable() or property.GetNumberOfProxies() > 1:
            return self[0:len(self)]
        else:
            if property.GetNumberOfProxies() > 0:
                return _getPyProxy(property.GetProxy(0))
        return None

    def SetData(self, values):
        """Allows setting of all values at once. Requires a single value,
        a tuple or list."""
        if isinstance(values, str):
            position = -1
            try:
                position = self.Available.index(values)
            except:
                raise ValueError, values + " is not a valid object in the domain."
            values = self.GetDomain('proxy_list').GetProxy(position)
        if not isinstance(values, tuple) and \
           not isinstance(values, list):
            values = (values,)
        self.SMProperty.RemoveAllProxies()
        for value in values:
            if isinstance(value, Proxy):
                value_proxy = value.SMProxy
            else:
                value_proxy = value
            self.SMProperty.AddProxy(value_proxy)
        self._UpdateProperty()

    def Clear(self):
        "Removes all elements."
        self.SMProperty.RemoveAllProxies()
        self._UpdateProperty()

class InputProperty(ProxyProperty):
    """An InputProperty allows making pipeline connections. You can set either
    a source proxy or an OutputProperty to an input property:

    > property[0] = proxy
    or
    > property[0] = OuputPort(proxy, 1)

    > property.append(proxy)
    or
    > property.append(OutputPort(proxy, 0))
    """
    def __setitem__(self, idx, value):
      """Given a list or tuple of values, sets a slice of values [min, max)"""
      if isinstance(idx, slice):
        indices = idx.indices(len(self))
        for i, j in zip(range(*indices), value):
          op = value[i-min]
          self.SMProperty.SetInputConnection(i, op.SMProxy, op.Port)
        self._UpdateProperty()
      elif idx >= len(self) or idx < 0:
        raise IndexError
      else:
        self.SMProperty.SetInputConnection(idx, value.SMProxy, value.Port)
        self._UpdateProperty()

    def __getitem__(self, idx):
      """Returns the range [min, max) of elements. Raises an IndexError
      exception if an argument is out of bounds."""
      if isinstance(idx, slice):
        indices = idx.indices(len(self))
        retVal = []
        for i in range(*indices):
            port = None
            if self.SMProperty.GetProxy(i):
                port = OutputPort(_getPyProxy(self.SMProperty.GetProxy(i)),\
                                  self.SMProperty.GetOutputPortForConnection(i))
            retVal.append(port)
        return retVal
      elif idx >= len(self) or idx < 0:
        raise IndexError
      return OutputPort(_getPyProxy(self.SMProperty.GetProxy(idx)),\
                        self.SMProperty.GetOutputPortForConnection(idx))

    def append(self, value):
        """Appends the given proxy to the property values.
        Accepts Proxy or OutputPort objects."""
        self.SMProperty.AddInputConnection(value.SMProxy, value.Port)
        self._UpdateProperty()

    def GetData(self):
        """Returns all elements as either a list of OutputPort objects or
        a single OutputPort object."""
        property = self.SMProperty
        if property.GetRepeatable() or property.GetNumberOfProxies() > 1:
            return self[0:len(self)]
        else:
            if property.GetNumberOfProxies() > 0:
                return OutputPort(_getPyProxy(property.GetProxy(0)),\
                                  self.SMProperty.GetOutputPortForConnection(0))
        return None

    def SetData(self, values):
        """Allows setting of all values at once. Requires a single value,
        a tuple or list. Accepts Proxy or OutputPort objects."""
        if isinstance(values, str):
            ProxyProperty.SetData(self, values)
            return
        if not isinstance(values, tuple) and \
           not isinstance(values, list):
            values = (values,)
        self.SMProperty.RemoveAllProxies()
        for value in values:
            if value:
                self.SMProperty.AddInputConnection(value.SMProxy, value.Port)
        self._UpdateProperty()

    def _UpdateProperty(self):
        "Pushes the value of this property to the server."
        ProxyProperty._UpdateProperty(self)
        iter = PropertyIterator(self.Proxy)
        for prop in iter:
            if isinstance(prop, ArraySelectionProperty):
                prop.UpdateDefault()


class DataInformation(object):
    """DataInformation is a contained for meta-data associated with an
    output data.

    DataInformation is a python wrapper around a vtkPVDataInformation.
    In addition to proving all methods of a vtkPVDataInformation, it provides
    a few convenience methods.

    Please note that some of the methods accessible through the DataInformation
    class are not listed by help() because the DataInformation objects forward
    unresolved attributes to the underlying object. To get the full list,
    see also dir(proxy.DataInformation).
    See also the doxygen based documentation of the vtkPVDataInformation C++
    class.
    """
    def __init__(self, dataInformation, proxy, idx):
        """Default constructor. Requires a vtkPVDataInformation, a source proxy
        and an output port id."""
        self.DataInformation = dataInformation
        self.Proxy = proxy
        self.Idx = idx

    def Update(self):
        """****Deprecated**** There is no reason anymore to use this method
        explicitly, it is called automatically when one gets any value from the
        data information object.
        Update the data information if necessary. Note that this
        does not cause execution of the underlying object. In certain
        cases, you may have to call UpdatePipeline() on the proxy."""
        if self.Proxy:
            self.Proxy.GetDataInformation(self.Idx)

    def GetDataSetType(self):
        """Returns the dataset type as defined in vtkDataObjectTypes."""
        self.Update()
        if not self.DataInformation:
            raise RuntimeError, "No data information is available"
        if self.DataInformation.GetCompositeDataSetType() > -1:
            return self.DataInformation.GetCompositeDataSetType()
        return self.DataInformation.GetDataSetType()

    def GetDataSetTypeAsString(self):
        """Returns the dataset type as a user-friendly string. This is
        not the same as the enumaration used by VTK"""
        return vtk.vtkDataObjectTypes.GetClassNameFromTypeId(self.GetDataSetType())

    def __getattr__(self, name):
        """Forwards unknown attribute requests to the underlying
        vtkPVInformation."""
        if not self.DataInformation:
            raise AttributeError("class has no attribute %s" % name)
            return None
        self.Update()
        return getattr(self.DataInformation, name)

class ArrayInformation(object):
    """Meta-information associated with an array. Use the Name
    attribute to get the array name.

    Please note that some of the methods accessible through the ArrayInformation
    class are not listed by help() because the ArrayInformation objects forward
    unresolved attributes to the underlying object.
    See the doxygen based documentation of the vtkPVArrayInformation C++
    class for a full list.
    """
    def __init__(self, proxy, field, name):
        self.Proxy = proxy
        self.FieldData = field
        self.Name = name

    def __getattr__(self, name):
        """Forward unknown methods to vtkPVArrayInformation"""
        array = self.FieldData.GetFieldData().GetArrayInformation(self.Name)
        if not array: return None
        return getattr(array, name)

    def __repr__(self):
        """Returns a user-friendly representation string."""
        return "Array: " + self.Name

    def GetRange(self, component=0):
        """Given a component, returns its value range as a tuple of 2 values."""
        array = self.FieldData.GetFieldData().GetArrayInformation(self.Name)
        range = array.GetComponentRange(component)
        return (range[0], range[1])

    if paraview.compatibility.GetVersion() <= 3.4:
       def Range(self, component=0):
           return self.GetRange(component)

class FieldDataInformationIterator(object):
    """Iterator for FieldDataInformation"""

    def __init__(self, info, items=False):
        self.FieldDataInformation = info
        self.index = 0
        self.items = items

    def __iter__(self):
        return self

    def next(self):
        if self.index >= self.FieldDataInformation.GetNumberOfArrays():
            raise StopIteration

        self.index += 1
        ai = self.FieldDataInformation[self.index-1]
        if self.items:
            return (ai.GetName(), ai)
        else:
            return ai


class FieldDataInformation(object):
    """Meta-data for a field of an output object (point data, cell data etc...).
    Provides easy access to the arrays using the slice interface:
    > narrays = len(field_info)
    > for i in range(narrays):
    >   array_info = field_info[i]

    Full slice interface is supported:
    > arrays = field_info[0:5:3]
    where arrays is a list.

    Array access by name is also possible:
    > array_info = field_info['Temperature']

    The number of arrays can also be accessed using the NumberOfArrays
    property.
    """
    def __init__(self, proxy, idx, field):
        self.Proxy = proxy
        self.OutputPort = idx
        self.FieldData = field

    def GetFieldData(self):
        """Convenience method to get the underlying
        vtkPVDataSetAttributesInformation"""
        return getattr(self.Proxy.GetDataInformation(self.OutputPort), "Get%sInformation" % self.FieldData)()

    def GetNumberOfArrays(self):
        """Returns the number of arrays."""
        self.Proxy.UpdatePipeline()
        return self.GetFieldData().GetNumberOfArrays()

    def GetArray(self, idx):
        """Given an index or a string, returns an array information.
        Raises IndexError if the index is out of bounds."""
        self.Proxy.UpdatePipeline()
        if not self.GetFieldData().GetArrayInformation(idx):
            return None
        if isinstance(idx, str):
            return ArrayInformation(self.Proxy, self, idx)
        elif idx >= len(self) or idx < 0:
            raise IndexError
        return ArrayInformation(self.Proxy, self, self.GetFieldData().GetArrayInformation(idx).GetName())

    def __len__(self):
        """Returns the number of arrays."""
        return self.GetNumberOfArrays()

    def __getitem__(self, idx):
        """Implements the [] operator. Accepts an array name."""
        if isinstance(idx, slice):
            indices = idx.indices(self.GetNumberOfArrays())
            retVal = []
            for i in range(*indices):
                retVal.append(self.GetArray(i))
            return retVal
        return self.GetArray(idx)

    def keys(self):
        """Implementation of the dictionary API"""
        kys = []
        narrays = self.GetNumberOfArrays()
        for i in range(narrays):
            kys.append(self.GetArray(i).GetName())
        return kys

    def values(self):
        """Implementation of the dictionary API"""
        vals = []
        narrays = self.GetNumberOfArrays()
        for i in range(narrays):
            vals.append(self.GetArray(i))
        return vals

    def iteritems(self):
        """Implementation of the dictionary API"""
        return FieldDataInformationIterator(self, True)

    def items(self):
        """Implementation of the dictionary API"""
        itms = []
        narrays = self.GetNumberOfArrays()
        for i in range(narrays):
            ai = self.GetArray(i)
            itms.append((ai.GetName(), ai))
        return itms

    def has_key(self, key):
        """Implementation of the dictionary API"""
        if self.GetArray(key):
            return True
        return False

    def __iter__(self):
        """Implementation of the dictionary API"""
        return FieldDataInformationIterator(self)

    def __getattr__(self, name):
        """Forwards unknown attributes to the underlying
        vtkPVDataSetAttributesInformation"""
        array = self.GetArray(name)
        if array: return array
        raise AttributeError("class has no attribute %s" % name)
        return None

    NumberOfArrays = property(GetNumberOfArrays, None, None, "Returns the number of arrays.")

def OutputPort(proxy, outputPort=0):
    if not Proxy:
        return None
    if isinstance(outputPort, str):
        outputPort = proxy.GetOutputPortIndex(outputPort)
    if outputPort >= proxy.GetNumberOfOutputPorts():
        return None
    if proxy.Port == outputPort:
        return proxy
    newinstance = _getPyProxy(proxy.SMProxy, outputPort)
    newinstance.Port = outputPort
    newinstance._Proxy__Properties = proxy._Proxy__Properties
    return newinstance

class ProxyManager(object):
    """When running scripts from the python shell in the ParaView application,
    registering proxies with the proxy manager is the ony mechanism to
    notify the graphical user interface (GUI) that a proxy
    exists. Therefore, unless a proxy is registered, it will not show up in
    the user interface. Also, the proxy manager is the only way to get
    access to proxies created using the GUI. Proxies created using the GUI
    are automatically registered under an appropriate group (sources,
    filters, representations and views). To get access to these objects,
    you can use proxyManager.GetProxy(group, name). The name is the same
    as the name shown in the pipeline browser.

    This class is a python wrapper for vtkSMProxyManager. Note that the
    underlying vtkSMProxyManager is a singleton. All instances of this
    class wil refer to the same object. In addition to all methods provided by
    vtkSMProxyManager (all unknown attribute requests are forwarded
    to the vtkSMProxyManager), this class provides several convenience
    methods.

    Please note that some of the methods accessible through the ProxyManager
    class are not listed by help() because the ProxyManager objects forwards
    unresolved attributes to the underlying object. To get the full list,
    see also dir(proxy.SMProxyManager). See also the doxygen based documentation
    of the vtkSMProxyManager C++ class.
    """

    def __init__(self):
        """Constructor. Assigned self.SMProxyManager to
        vtkSMObject.GetPropertyManager()."""
        self.SMProxyManager = vtkSMObject.GetProxyManager()

    def RegisterProxy(self, group, name, aProxy):
        """Registers a proxy (either SMProxy or proxy) with the
        server manager"""
        if isinstance(aProxy, Proxy):
            self.SMProxyManager.RegisterProxy(group, name, aProxy.SMProxy)
        else:
            self.SMProxyManager.RegisterProxy(group, name, aProxy)

    def NewProxy(self, group, name):
        """Creates a new proxy of given group and name and returns an SMProxy.
        Note that this is a server manager object. You should normally create
        proxies using the class objects. For example:
        obj = servermanager.sources.SphereSource()"""
        if not self.SMProxyManager:
            return None
        aProxy = self.SMProxyManager.NewProxy(group, name)
        if not aProxy:
            return None
        aProxy.UnRegister(None)
        return aProxy

    def GetProxy(self, group, name):
        """Returns a Proxy registered under a group and name"""
        if not self.SMProxyManager:
            return None
        aProxy = self.SMProxyManager.GetProxy(group, name)
        if not aProxy:
            return None
        return _getPyProxy(aProxy)

    def GetPrototypeProxy(self, group, name):
        """Returns a prototype proxy given a group and name. This is an
        SMProxy. This is a low-level method. You should not normally
        have to call it."""
        if not self.SMProxyManager:
            return None
        aProxy = self.SMProxyManager.GetPrototypeProxy(group, name)
        if not aProxy:
            return None
        return aProxy

    def GetProxiesOnConnection(self, connection):
        """Returns a map of proxies registered with the proxy manager
           on the particular connection."""
        proxy_groups = {}
        iter = self.NewConnectionIterator(connection)
        for proxy in iter:
            if not proxy_groups.has_key(iter.GetGroup()):
                proxy_groups[iter.GetGroup()] = {}
            group = proxy_groups[iter.GetGroup()]
            group[(iter.GetKey(), proxy.GetSelfIDAsString())] = proxy
        return proxy_groups

    def GetProxiesInGroup(self, groupname, connection=None):
        """Returns a map of proxies in a particular group.
         If connection is not None, then only those proxies
         in the group that are on the particular connection
         are returned.
        """
        proxies = {}
        iter = self.NewGroupIterator(groupname)
        for aProxy in iter:
            proxies[(iter.GetKey(), aProxy.GetSelfIDAsString())] = aProxy
        return proxies

    def UnRegisterProxy(self, groupname, proxyname, aProxy):
        """Unregisters a proxy."""
        if not self.SMProxyManager:
            return
        if aProxy != None and isinstance(aProxy,Proxy):
            aProxy = aProxy.SMProxy
        if aProxy:
            self.SMProxyManager.UnRegisterProxy(groupname, proxyname, aProxy)

    def GetProxies(self, groupname, proxyname):
        """Returns all proxies registered under the given group with the
        given name. Note that it is possible to register more than one
        proxy with the same name in the same group. Because the proxies
        are different, there is no conflict. Use this method instead of
        GetProxy() if you know that there are more than one proxy registered
        with this name."""
        if not self.SMProxyManager:
            return []
        collection = vtk.vtkCollection()
        result = []
        self.SMProxyManager.GetProxies(groupname, proxyname, collection)
        for i in range(0, collection.GetNumberOfItems()):
            aProxy = _getPyProxy(collection.GetItemAsObject(i))
            if aProxy:
                result.append(aProxy)

        return result

    def __iter__(self):
        """Returns a new ProxyIterator."""
        iter = ProxyIterator()
        if ActiveConnection:
            iter.SetConnectionID(ActiveConnection.ID)
        iter.Begin()
        return iter

    def NewGroupIterator(self, group_name, connection=None):
        """Returns a ProxyIterator for a group. The resulting object
        can be used to traverse the proxies that are in the given
        group."""
        iter = self.__iter__()
        if not connection:
            connection = ActiveConnection
        if connection:
            iter.SetConnectionID(connection.ID)
        iter.SetModeToOneGroup()
        iter.Begin(group_name)
        return iter

    def NewConnectionIterator(self, connection=None):
        """Returns a ProxyIterator for a given connection. This can be
        used to travers ALL proxies managed by the proxy manager."""
        iter = self.__iter__()
        if not connection:
            connection = ActiveConnection
        if connection:
            iter.SetConnectionID(connection.ID)
        iter.Begin()
        return iter

    def NewDefinitionIterator(self, groupname=None):
        """Returns an iterator that can be used to iterate over
           all groups and types of proxies that the proxy manager
           can create."""
        iter = ProxyDefinitionIterator()
        if groupname != None:
            iter.SetModeToOneGroup()
            iter.Begin(groupname)
        return iter

    def __ConvertArgumentsAndCall(self, *args):
      newArgs = []
      for arg in args:
          if issubclass(type(arg), Proxy) or isinstance(arg, Proxy):
              newArgs.append(arg.SMProxy)
          else:
              newArgs.append(arg)
      func = getattr(self.SMProxyManager, self.__LastAttrName)
      retVal = func(*newArgs)
      if type(retVal) is type(self.SMProxyManager) and retVal.IsA("vtkSMProxy"):
          return _getPyProxy(retVal)
      else:
          return retVal

    def __getattr__(self, name):
        """Returns attribute from the ProxyManager"""
        try:
            pmAttr = getattr(self.SMProxyManager, name)
            self.__LastAttrName = name
            return self.__ConvertArgumentsAndCall
        except:
            pass
        return getattr(self.SMProxyManager, name)


class PropertyIterator(object):
    """Wrapper for a vtkSMPropertyIterator class to satisfy
       the python iterator protocol. Note that the list of
       properties can also be obtained from the class object's
       dictionary.
       See the doxygen documentation for vtkSMPropertyIterator C++
       class for details.
       """

    def __init__(self, aProxy):
        self.SMIterator = aProxy.NewPropertyIterator()
        if self.SMIterator:
            self.SMIterator.UnRegister(None)
            self.SMIterator.Begin()
        self.Key = None
        self.PropertyLabel = None
        self.Proxy = aProxy

    def __iter__(self):
        return self

    def next(self):
        if not self.SMIterator:
            raise StopIteration

        if self.SMIterator.IsAtEnd():
            self.Key = None
            raise StopIteration
        self.Key = self.SMIterator.GetKey()
        self.PropertyLabel = self.SMIterator.GetPropertyLabel()
        self.SMIterator.Next()
        return self.Proxy.GetProperty(self.Key)

    def GetProxy(self):
        """Returns the proxy for the property last returned by the call to
        'next()'"""
        return self.Proxy

    def GetKey(self):
        """Returns the key for the property last returned by the call to
        'next()' """
        return self.Key

    def GetProperty(self):
        """Returns the property last returned by the call to 'next()' """
        return self.Proxy.GetProperty(self.Key)

    def __getattr__(self, name):
        """returns attributes from the vtkSMProxyIterator."""
        return getattr(self.SMIterator, name)

class ProxyDefinitionIterator(object):
    """Wrapper for a vtkSMProxyDefinitionIterator class to satisfy
       the python iterator protocol.
       See the doxygen documentation of the vtkSMProxyDefinitionIterator
       C++ class for more information."""
    def __init__(self):
        self.SMIterator = vtkSMProxyDefinitionIterator()
        self.Group = None
        self.Key = None

    def __iter__(self):
        return self

    def next(self):
        if self.SMIterator.IsAtEnd():
            self.Group = None
            self.Key = None
            raise StopIteration
        self.Group = self.SMIterator.GetGroup()
        self.Key = self.SMIterator.GetKey()
        self.SMIterator.Next()
        return {"group": self.Group, "key":self.Key }

    def GetKey(self):
        """Returns the key for the proxy definition last returned by the call
        to 'next()' """
        return self.Key

    def GetGroup(self):
        """Returns the group for the proxy definition last returned by the
        call to 'next()' """
        return self.Group

    def __getattr__(self, name):
        """returns attributes from the vtkSMProxyDefinitionIterator."""
        return getattr(self.SMIterator, name)


class ProxyIterator(object):
    """Wrapper for a vtkSMProxyIterator class to satisfy the
     python iterator protocol.
     See the doxygen documentation of vtkSMProxyIterator C++ class for
     more information.
     """
    def __init__(self):
        self.SMIterator = vtkSMProxyIterator()
        self.SMIterator.Begin()
        self.AProxy = None
        self.Group = None
        self.Key = None

    def __iter__(self):
        return self

    def next(self):
        if self.SMIterator.IsAtEnd():
            self.AProxy = None
            self.Group = None
            self.Key = None
            raise StopIteration
            return None
        self.AProxy = _getPyProxy(self.SMIterator.GetProxy())
        self.Group = self.SMIterator.GetGroup()
        self.Key = self.SMIterator.GetKey()
        self.SMIterator.Next()
        return self.AProxy

    def GetProxy(self):
        """Returns the proxy last returned by the call to 'next()'"""
        return self.AProxy

    def GetKey(self):
        """Returns the key for the proxy last returned by the call to
        'next()' """
        return self.Key

    def GetGroup(self):
        """Returns the group for the proxy last returned by the call to
        'next()' """
        return self.Group

    def __getattr__(self, name):
        """returns attributes from the vtkSMProxyIterator."""
        return getattr(self.SMIterator, name)

class Connection(object):
    """
      This is a python representation for a connection.
    """
    def __init__(self, connectionId):
        """Default constructor. Creates a Connection with the given
        ID, all other data members initialized to None."""
        self.ID = connectionId
        self.Hostname = None
        self.Port = None
        self.RSHostname = None
        self.RSPort = None
        self.Reverse = False
        return

    def __eq__(self, other):
        "Returns true if the connection ids are the same."
        return self.ID == other.ID

    def SetHost(self, ds_host=None, ds_port=None, rs_host=None, rs_port=None,
      reverse=False):
        """
          Set the hostname of a given connection. Used by Connect().
          If all args are None, it's assumed to be a built-in connection i.e.
          connection scheme = builtin.
        """
        self.Hostname = ds_host
        self.Port = ds_port
        self.RSHostname = rs_host
        self.RSPort = rs_port
        self.Reversed = reverse
        return

    def __repr__(self):
        """User friendly string representation"""
        if not self.Hostname:
           return "Connection (builtin[%d]:)" % self.ID
        if not self.RSHostname:
            return "Connection (%s:%d)" % (self.Hostname, self.Port)
        return "Connection data(%s:%d), render(%s:%d)" % \
            (self.Hostname, self.Port, self.RSHostname, self.RSPort)

    def GetURI(self):
        """Get URI of the connection"""
        if not self.Hostname or self.Hostname == "builtin":
            return "builtin:"
        if self.Reversed:
            if not self.RSHostname:
                return "csrc://%s:%d" % (self.Hostname, self.Port)
            return "cdsrsrc://%s:%d//%s:%d" % (self.Hostname, self.Port,
              self.RSHostname, self.RSPort)
        if not self.RSHostname:
            return "cs://%s:%d" % (self.Hostname, self.Port)
        return "cdsrs://%s:%d//%s:%d" % (self.Hostname, self.Port,
          self.RSHostname, self.RSPort)

    def IsRemote(self):
        """Returns True if the connection to a remote server, False if
        it is local (built-in)"""
        pm = vtkProcessModule.GetProcessModule()
        if pm.IsRemote(self.ID):
            return True
        return False

    def GetNumberOfDataPartitions(self):
        """Returns the number of partitions on the data server for this
           connection"""
        pm = vtkProcessModule.GetProcessModule()
        return pm.GetNumberOfPartitions(self.ID)


## These are methods to create a new connection.
## One can connect to a server, (data-server,render-server)
## or simply create a built-in connection.
## Note: these are internal methods. Use Connect() instead.
def _connectServer(host, port, rc=False):
    """Connect to a host:port. Returns the connection object if successfully
    connected with the server. Internal method, use Connect() instead."""
    pm =  vtkProcessModule.GetProcessModule()
    if not rc:
        cid = pm.ConnectToRemote(host, port)
        if not cid:
            return None
        conn = Connection(cid)
    else:
        pm.AcceptConnectionsOnPort(port)
        print "Waiting for connection..."
        while True:
            cid = pm.MonitorConnections(10)
            if cid > 0:
                conn = Connection(cid)
                break
        pm.StopAcceptingAllConnections()
    conn.SetHost(host, port, None, None, rc)
    return conn

def _connectDsRs(ds_host, ds_port, rs_host, rs_port):
    """Connect to a dataserver at (ds_host:ds_port) and to a render server
    at (rs_host:rs_port).
    Returns the connection object if successfully connected
    with the server. Internal method, use Connect() instead."""
    pm =  vtkProcessModule.GetProcessModule()
    cid = pm.ConnectToRemote(ds_host, ds_port, rs_host, rs_port)
    if not cid:
        return None
    conn = Connection(cid)
    conn.SetHost(ds_host, ds_port, rs_host, rs_port)
    return conn

def _connectSelf():
    """Creates a new self connection.Internal method, use Connect() instead."""
    pm =  vtkProcessModule.GetProcessModule()
    pmOptions = pm.GetOptions()
    if pmOptions.GetProcessType() == 0x40: # PVBATCH
        return Connection(vtkProcessModuleConnectionManager.GetRootServerConnectionID())
    cid = pm.ConnectToSelf()
    if not cid:
        return None
    conn = Connection(cid)
    conn.SetHost("builtin", cid)
    return conn

def SaveState(filename):
    """Given a state filename, saves the state of objects registered
    with the proxy manager."""
    pm = ProxyManager()
    pm.SaveState(filename)

def LoadState(filename, connection=None):
    """Given a state filename and an optional connection, loads the server
    manager state."""
    if not connection:
        connection = ActiveConnection
    if not connection:
        raise RuntimeError, "Cannot load state without a connection"
    loader = vtkSMPQStateLoader()
    pm = ProxyManager()
    pm.LoadState(filename, ActiveConnection.ID, loader)
    views = GetRenderViews()
    for view in views:
        # Make sure that the client window size matches the
        # ViewSize property. In paraview, the GUI takes care
        # of this.
        if view.GetClassName() == "vtkSMIceTDesktopRenderViewProxy":
            view.GetRenderWindow().SetSize(view.ViewSize[0], \
                                           view.ViewSize[1])

def Connect(ds_host=None, ds_port=11111, rs_host=None, rs_port=11111):
    """
    Use this function call to create a new connection. On success,
    it returns a Connection object that abstracts the connection.
    Otherwise, it returns None.
    There are several ways in which this function can be called:
    * When called with no arguments, it creates a new connection
      to the built-in server on the client itself.
    * When called with ds_host and ds_port arguments, it
      attempts to connect to a server(data and render server on the same server)
      on the indicated host:port.
    * When called with ds_host, ds_port, rs_host, rs_port, it
      creates a new connection to the data server on ds_host:ds_port and to the
      render server on rs_host: rs_port.
    """
    global ActiveConnection
    global fromGUI
    if fromGUI:
        raise RuntimeError, "Cannot create a connection through python. Use the GUI to setup the connection."
    if ds_host == None:
        connectionId = _connectSelf()
    elif rs_host == None:
        connectionId = _connectServer(ds_host, ds_port)
    else:
        connectionId = _connectDsRs(ds_host, ds_port, rs_host, rs_port)
    if not ActiveConnection:
        ActiveConnection = connectionId
    return connectionId

def ReverseConnect(port=11111):
    """
    Use this function call to create a new connection. On success,
    it returns a Connection object that abstracts the connection.
    Otherwise, it returns None.
    In reverse connection mode, the client waits for a connection
    from the server (client has to be started first). The server
    then connects to the client (run pvserver with -rc and -ch
    option).
    The optional port specified the port to listen to.
    """
    global ActiveConnection
    global fromGUI
    if fromGUI:
        raise RuntimeError, "Cannot create a connection through python. Use the GUI to setup the connection."
    connectionId = _connectServer("Reverse connection", port, True)
    if not ActiveConnection:
        ActiveConnection = connectionId
    return connectionId

def Disconnect(connection=None):
    """Disconnects the connection. Make sure to clear the proxy manager
    first."""
    global ActiveConnection
    global fromGUI
    if fromGUI:
        raise RuntimeError, "Cannot disconnect through python. Use the GUI to disconnect."
    if not connection or connection == ActiveConnection:
        connection = ActiveConnection
        ActiveConnection = None
    if connection:
        pm =  vtkProcessModule.GetProcessModule()
        pm.Disconnect(connection.ID)
    return

def CreateProxy(xml_group, xml_name, connection=None):
    """Creates a proxy. If connection is set, the proxy's connection ID is
    set accordingly. If connection is None, ActiveConnection is used, if
    present. You should not have to use method normally. Instantiate the
    appropriate class from the appropriate module, for example:
    sph = servermanager.sources.SphereSource()"""

    pxm = ProxyManager()
    aProxy = pxm.NewProxy(xml_group, xml_name)
    if not aProxy:
        return None
    if not connection:
        connection = ActiveConnection
    if connection:
        aProxy.SetConnectionID(connection.ID)
    return aProxy

def GetRenderView(connection=None):
    """Return the render view in use.  If more than one render view is in
    use, return the first one."""

    if not connection:
        connection = ActiveConnection
    render_module = None
    for aProxy in ProxyManager().NewConnectionIterator(connection):
        if aProxy.IsA("vtkSMRenderViewProxy"):
            render_module = aProxy
            break
    return render_module

def GetRenderViews(connection=None):
    """Returns the set of all render views."""

    if not connection:
        connection = ActiveConnection
    render_modules = []
    for aProxy in ProxyManager().NewConnectionIterator(connection):
        if aProxy.IsA("vtkSMRenderViewProxy"):
            render_modules.append(aProxy)
    return render_modules

def CreateRenderView(connection=None, **extraArgs):
    """Creates a render window on the particular connection. If connection
    is not specified, then the active connection is used, if available.

    This method can also be used to initialize properties by passing
    keyword arguments where the key is the name of the property. In addition
    registrationGroup and registrationName (optional) can be specified (as
    keyword arguments) to automatically register the proxy with the proxy
    manager."""
    return _create_view("RenderView", connection, **extraArgs)

def _create_view(view_xml_name, connection=None, **extraArgs):
    """Creates a view on the particular connection. If connection
    is not specified, then the active connection is used, if available.
    This method can also be used to initialize properties by passing
    keyword arguments where the key is the name of the property."""
    if not connection:
        connection = ActiveConnection
    if not connection:
        raise RuntimeError, "Cannot create view without connection."
    pxm = ProxyManager()
    prototype = pxm.GetPrototypeProxy("views", view_xml_name)
    proxy_xml_name = prototype.GetSuggestedViewType(connection.ID)
    view_module = None
    if proxy_xml_name:
        view_module = CreateProxy("views", proxy_xml_name, connection)
    if not view_module:
        return None
    extraArgs['proxy'] = view_module
    proxy = rendering.__dict__[view_module.GetXMLName()](**extraArgs)
    return proxy

def GetRepresentation(aProxy, view):
    for rep in view.Representations:
        try: isRep = rep.Input == aProxy
        except: isRep = False
        if isRep: return rep
    return None

def CreateRepresentation(aProxy, view, **extraArgs):
    """Creates a representation for the proxy and adds it to the render
    module.

    This method can also be used to initialize properties by passing
    keyword arguments where the key is the name of the property.In addition
    registrationGroup and registrationName (optional) can be specified (as
    keyword arguments) to automatically register the proxy with the proxy
    manager.

    This method tries to create the best possible representation for the given
    proxy in the given view. Additionally, the user can specify proxyName
    (optional) to create a representation of a particular type."""

    global rendering
    if not aProxy:
        raise RuntimeError, "proxy argument cannot be None."
    if not view:
        raise RuntimeError, "view argument cannot be None."
    if "proxyName" in extraArgs:
      display = CreateProxy("representations", extraArgs['proxyName'], None)
      del extraArgs['proxyName']
    else:
      display = view.SMProxy.CreateDefaultRepresentation(aProxy.SMProxy, 0)
      if display:
        display.UnRegister(None)
    if not display:
        return None
    display.SetConnectionID(aProxy.GetConnectionID())
    extraArgs['proxy'] = display
    proxy = rendering.__dict__[display.GetXMLName()](**extraArgs)
    proxy.Input = aProxy
    proxy.UpdateVTKObjects()
    view.Representations.append(proxy)
    return proxy

class _ModuleLoader(object):
    def find_module(self, fullname, path=None):
        if vtkPVPythonModule.HasModule(fullname):
            return self
        else:
            return None
    def load_module(self, fullname):
        import imp
        moduleInfo = vtkPVPythonModule.GetModule(fullname)
        if not moduleInfo:
            raise ImportError
        module = sys.modules.setdefault(fullname, imp.new_module(fullname))
        module.__file__ = "<%s>" % moduleInfo.GetFullName()
        module.__loader__ = self
        if moduleInfo.GetIsPackage:
            module.__path__ = moduleInfo.GetFullName()
        code = compile(moduleInfo.GetSource(), module.__file__, 'exec')
        exec code in module.__dict__
        return module

def LoadXML(xmlstring):
    """Given a server manager XML as a string, parse and process it."""
    parser = vtkSMXMLParser()
    if not parser.Parse(xmlstring):
        raise RuntimeError, "Problem parsing XML string."
    parser.ProcessConfiguration(vtkSMObject.GetProxyManager())
    # Update the modules
    updateModules()

def LoadPlugin(filename,  remote=True, connection=None):
    """ Given a filename and a connection (optional, otherwise uses
    ActiveConnection), loads a plugin. It then updates the sources,
    filters and rendering modules."""

    if not connection:
        connection = ActiveConnection
    if not connection:
        raise RuntimeError, "Cannot load a plugin without a connection."

    pxm=ProxyManager()
    plm=pxm.GetApplication().GetPluginManager()
    
    """ Load the plugin on server. """
    if remote:
      serverURI = connection.GetURI()
    else:
      serverURI = "builtin:"
    
    plinfo = plm.LoadPlugin(filename, connection.ID, serverURI, remote)
    
    if not plinfo or not plinfo.GetLoaded():
        # Assume that it is an xml file
        f = open(filename, 'r')
        try:
            LoadXML(f.read())
        except RuntimeError:
            raise RuntimeError, "Problem loading plugin %s: %s" % (filename, pld.GetProperty("Error").GetElement(0))
    else:
        updateModules()


def Fetch(input, arg1=None, arg2=None, idx=0):
    """
    A convenience method that moves data from the server to the client,
    optionally performing some operation on the data as it moves.
    The input argument is the name of the (proxy for a) source or filter
    whose output is needed on the client.

    You can use Fetch to do three things:

    If arg1 is None (the default) then all of the data is brought to the client.
    In parallel runs an appropriate append Filter merges the
    data on each processor into one data object. The filter chosen will be
    vtkAppendPolyData for vtkPolyData, vtkAppendRectilinearGrid for
    vtkRectilinearGrid, vtkMultiBlockDataGroupFilter for vtkCompositeData,
    and vtkAppendFilter for anything else.

    If arg1 is an integer then one particular processor's output is brought to
    the client. In serial runs the arg is ignored. If you have a filter that
    computes results in parallel and brings them to the root node, then set
    arg to be 0.

    If arg1 and arg2 are a algorithms, for example vtkMinMax, the algorithm
    will be applied to the data to obtain some result. Here arg1 will be
    applied pre-gather and arg2 will be applied post-gather. In parallel
    runs the algorithm will be run on each processor to make intermediate
    results and then again on the root processor over all of the
    intermediate results to create a global result.

    Optional argument idx is used to specify the output port number to fetch the
    data from. Default is port 0.
    """

    import types

    #create the pipeline that reduces and transmits the data
    gvd = rendering.ClientDeliveryRepresentationBase()
    gvd.AddInput(0, input, idx, "DONTCARE")

    if arg1 == None:
        print "getting appended"

        cdinfo = input.GetDataInformation(idx).GetCompositeDataInformation()
        if cdinfo.GetDataIsComposite():
            print "use composite data append"
            gvd.SetReductionType(5)

        elif input.GetDataInformation(idx).GetDataClassName() == "vtkPolyData":
            print "use append poly data filter"
            gvd.SetReductionType(1)

        elif input.GetDataInformation(idx).GetDataClassName() == "vtkRectilinearGrid":
            print "use append rectilinear grid filter"
            gvd.SetReductionType(4)

        elif input.GetDataInformation(idx).IsA("vtkDataSet"):
            print "use unstructured append filter"
            gvd.SetReductionType(2)


    elif type(arg1) is types.IntType:
        print "getting node %d" % arg1
        gvd.SetReductionType(3)
        gvd.SetPreGatherHelper(None)
        gvd.SetPostGatherHelper(None)
        gvd.SetPassThrough(arg1)

    else:
        print "applying operation"
        gvd.SetReductionType(6) # CUSTOM
        gvd.SetPreGatherHelper(arg1)
        gvd.SetPostGatherHelper(arg2)
        gvd.SetPassThrough(-1)

    #go!
    gvd.UpdateVTKObjects()
    gvd.Update()
    op = gvd.GetOutput()
    opc = gvd.GetOutput().NewInstance()
    opc.ShallowCopy(op)
    opc.UnRegister(None)
    return opc

def AnimateReader(reader, view, filename=None):
    """This is a utility function that, given a reader and a view
    animates over all time steps of the reader. If the optional
    filename is provided, a movie is created (type depends on the
    extension of the filename."""
    if not reader:
        raise RuntimeError, "No reader was specified, cannot animate."
    if not view:
        raise RuntimeError, "No view was specified, cannot animate."
    # Create an animation scene
    scene = animation.AnimationScene()

    # We need to have the reader and the view registered with
    # the time keeper. This is how the scene gets its time values.
    try:
        tk = ProxyManager().GetProxiesInGroup("timekeeper").values()[0]
        scene.TimeKeeper = tk
    except IndexError:
        tk = misc.TimeKeeper()
        scene.TimeKeeper = tk

    if not reader in tk.TimeSources:
        tk.TimeSources.append(reader)
    if not view in tk.Views:
        tk.Views.append(view)


    # with 1 view
    scene.ViewModules = [view]
    # Update the reader to get the time information
    reader.UpdatePipelineInformation()
    # Animate from 1st time step to last
    scene.StartTime = reader.TimestepValues.GetData()[0]
    scene.EndTime = reader.TimestepValues.GetData()[-1]

    # Each frame will correspond to a time step
    scene.PlayMode = 2 #Snap To Timesteps

    # Create a special animation cue for time.
    cue = animation.TimeAnimationCue()
    cue.AnimatedProxy = view
    cue.AnimatedPropertyName = "ViewTime"
    scene.Cues = [cue]

    if filename:
        writer = vtkSMAnimationSceneImageWriter()
        writer.SetFileName(filename)
        writer.SetFrameRate(1)
        writer.SetAnimationScene(scene.SMProxy)

        # Now save the animation.
        if not writer.Save():
            raise RuntimeError, "Saving of animation failed!"
    else:
        scene.Play()
    return scene

def GetProgressPrintingIsEnabled():
    return progressObserverTag is not None

def SetProgressPrintingEnabled(value):
    """Turn on/off printing of progress (by default, it is on). You can
    always turn progress off and add your own observer to the process
    module to handle progress in a custom way. See _printProgress for
    an example event observer."""
    global progressObserverTag

    # If value is true and progress printing is currently off...
    if value and not GetProgressPrintingIsEnabled():
        if fromGUI:
            raise RuntimeError("Printing progress in the GUI is not supported.")
        progressObserverTag = vtkProcessModule.GetProcessModule().AddObserver(\
            "ProgressEvent", _printProgress)

    # If value is false and progress printing is currently on...
    elif GetProgressPrintingIsEnabled():
        vtkProcessModule.GetProcessModule().RemoveObserver(progressObserverTag)
        progressObserverTag = None

def ToggleProgressPrinting():
    """Turn on/off printing of progress.  See SetProgressPrintingEnabled."""
    SetProgressPrintingEnabled(not GetProgressPrintingIsEnabled())

def Finalize():
    """Although not required, this can be called at exit to cleanup."""
    global progressObserverTag
    # Make sure to remove the observer
    if progressObserverTag:
        ToggleProgressPrinting()
    vtkInitializationHelper.Finalize()

# Internal methods

def _getPyProxy(smproxy, outputPort=0):
    """Returns a python wrapper for a server manager proxy. This method
    first checks if there is already such an object by looking in the
    _pyproxies group and returns it if found. Otherwise, it creates a
    new one. Proxies register themselves in _pyproxies upon creation."""
    if not smproxy:
        return None
    if (smproxy, outputPort) in _pyproxies:
        return _pyproxies[(smproxy, outputPort)]()

    xmlName = smproxy.GetXMLName()
    if paraview.compatibility.GetVersion() >= 3.5:
        if smproxy.GetXMLLabel():
            xmlName = smproxy.GetXMLLabel()
    classForProxy = _findClassForProxy(_make_name_valid(xmlName), smproxy.GetXMLGroup())
    if classForProxy:
        retVal = classForProxy(proxy=smproxy, port=outputPort)
    else:
        retVal = Proxy(proxy=smproxy, port=outputPort)
    return retVal

def _makeUpdateCameraMethod(rv):
    """ This internal method is used to create observer methods """
    if not hasattr(rv(), "BlockUpdateCamera"):
        rv().add_attribute("BlockUpdateCamera", False)
    def UpdateCamera(obj, string):
        if not rv().BlockUpdateCamera:
          # used to avoid some nasty recursion that occurs when interacting in
          # the GUI.
          rv().BlockUpdateCamera = True
          rv().SynchronizeCameraProperties()
          rv().BlockUpdateCamera = False
    return UpdateCamera

def _createInitialize(group, name):
    """Internal method to create an Initialize() method for the sub-classes
    of Proxy"""
    pgroup = group
    pname = name
    def aInitialize(self, connection=None):
        if not connection:
            connection = ActiveConnection
        if not connection:
            raise RuntimeError,\
                  'Cannot create a proxy without a connection.'
        self.InitializeFromProxy(\
            CreateProxy(pgroup, pname, connection))
    return aInitialize

def _createGetProperty(pName):
    """Internal method to create a GetXXX() method where XXX == pName."""
    propName = pName
    def getProperty(self):
        if paraview.compatibility.GetVersion() >= 3.5:
            return self.GetPropertyValue(propName)
        else:
            return self.GetProperty(propName)
    return getProperty

def _createSetProperty(pName):
    """Internal method to create a SetXXX() method where XXX == pName."""
    propName = pName
    def setProperty(self, value):
        return self.SetPropertyWithName(propName, value)
    return setProperty

def _findClassForProxy(xmlName, xmlGroup):
    """Given the xmlName for a proxy, returns a Proxy class. Note
    that if there are duplicates, the first one is returned."""
    global sources, filters, writers, rendering, animation, implicit_functions,\
           piecewise_functions, extended_sources, misc
    if not xmlName:
        return None
    if xmlGroup == "sources":
        return sources.__dict__[xmlName]
    elif xmlGroup == "filters":
        return filters.__dict__[xmlName]
    elif xmlGroup == "implicit_functions":
        return implicit_functions.__dict__[xmlName]
    elif xmlGroup == "piecewise_functions":
        return piecewise_functions.__dict__[xmlName]
    elif xmlGroup == "writers":
        return writers.__dict__[xmlName]
    elif xmlGroup == "extended_sources":
        return extended_sources.__dict__[xmlName]
    elif xmlName in rendering.__dict__:
        return rendering.__dict__[xmlName]
    elif xmlName in animation.__dict__:
        return animation.__dict__[xmlName]
    elif xmlName in misc.__dict__:
        return misc.__dict__[xmlName]
    else:
        return None

def _printProgress(caller, event):
    """The default event handler for progress. Prints algorithm
    name and 1 '.' per 10% progress."""
    global currentAlgorithm, currentProgress

    pm = vtkProcessModule.GetProcessModule()
    progress = pm.GetLastProgress() / 10
    # If we got a 100% as the first thing, ignore
    # This is to get around the fact that some vtk
    # algorithms report 100% more than once (which is
    # a bug)
    if not currentAlgorithm and progress == 10:
        return
    alg = pm.GetLastProgressName()
    if alg != currentAlgorithm and alg:
        if currentAlgorithm:
            while currentProgress <= 10:
                import sys
                sys.stdout.write(".")
                currentProgress += 1
            print "]"
            currentProgress = 0
        print alg, ": [ ",
        currentAlgorithm = alg
    while currentProgress <= progress:
        import sys
        sys.stdout.write(".")
        #sys.stdout.write("%d " % pm.GetLastProgress())
        currentProgress += 1
    if progress == 10:
        print "]"
        currentAlgorithm = None
        currentProgress = 0

def updateModules():
    """Called when a plugin is loaded, this method updates
    the proxy class object in all known modules."""
    global sources, filters, writers, rendering, animation, implicit_functions,\
           piecewise_functions, extended_sources, misc

    createModule("sources", sources)
    createModule("filters", filters)
    createModule("writers", writers)
    createModule("representations", rendering)
    createModule("views", rendering)
    createModule("lookup_tables", rendering)
    createModule("textures", rendering)
    createModule("animation", animation)
    createModule("misc", misc)
    createModule('animation_keyframes', animation)
    createModule('implicit_functions', implicit_functions)
    createModule('piecewise_functions', piecewise_functions)
    createModule("extended_sources", extended_sources)
    createModule("incremental_point_locators", misc)

def _createModules():
    """Called when the module is loaded, this creates sub-
    modules for all know proxy groups."""
    global sources, filters, writers, rendering, animation, implicit_functions,\
           piecewise_functions, extended_sources, misc

    sources = createModule('sources')
    filters = createModule('filters')
    writers = createModule('writers')
    rendering = createModule('representations')
    createModule('views', rendering)
    createModule("lookup_tables", rendering)
    createModule("textures", rendering)
    animation = createModule('animation')
    createModule('animation_keyframes', animation)
    implicit_functions = createModule('implicit_functions')
    piecewise_functions = createModule('piecewise_functions')
    extended_sources = createModule("extended_sources")
    misc = createModule("misc")
    createModule("incremental_point_locators", misc)

class PVModule(object):
    pass

def _make_name_valid(name):
    return paraview.make_name_valid(name)

def createModule(groupName, mdl=None):
    """Populates a module with proxy classes defined in the given group.
    If mdl is not specified, it also creates the module"""

    pxm = vtkSMObject.GetProxyManager()
    # Use prototypes to find all proxy types.
    pxm.InstantiateGroupPrototypes(groupName)

    debug = False
    if not mdl:
        debug = True
        mdl = PVModule()
    numProxies = pxm.GetNumberOfXMLProxies(groupName)
    for i in range(numProxies):
        proxyName = pxm.GetXMLProxyName(groupName, i)
        proto = pxm.GetPrototypeProxy(groupName, proxyName)
        pname = proxyName
        if paraview.compatibility.GetVersion() >= 3.5 and\
           proto.GetXMLLabel():
            pname = proto.GetXMLLabel()
        pname = _make_name_valid(pname)
        if not pname:
            continue
        if pname in mdl.__dict__:
            if debug:
                print "Warning: %s is being overwritten. This may point to an issue in the ParaView configuration files" % pname
        cdict = {}
        # Create an Initialize() method for this sub-class.
        cdict['Initialize'] = _createInitialize(groupName, proxyName)
        iter = PropertyIterator(proto)
        # Add all properties as python properties.
        for prop in iter:
            propName = iter.GetKey()
            if paraview.compatibility.GetVersion() >= 3.5:
                if (prop.GetInformationOnly() and propName != "TimestepValues" ) \
                  or prop.GetIsInternal():
                    continue
            names = [propName]
            if paraview.compatibility.GetVersion() >= 3.5:
                names = [iter.PropertyLabel]
                
            propDoc = None
            if prop.GetDocumentation():
                propDoc = prop.GetDocumentation().GetDescription()
            for name in names:
                name = _make_name_valid(name)
                if name:
                    cdict[name] = property(_createGetProperty(propName),
                                           _createSetProperty(propName),
                                           None,
                                           propDoc)
        # Add the documentation as the class __doc__
        if proto.GetDocumentation() and \
           proto.GetDocumentation().GetDescription():
            doc = proto.GetDocumentation().GetDescription()
        else:
            doc = Proxy.__doc__
        cdict['__doc__'] = doc
        # Create the new type
        if proto.GetXMLName() == "ExodusIIReader":
            superclasses = (ExodusIIReaderProxy,)
        elif proto.IsA("vtkSMSourceProxy"):
            superclasses = (SourceProxy,)
        else:
            superclasses = (Proxy,)

        cobj = type(pname, superclasses, cdict)
        # Add it to the modules dictionary
        mdl.__dict__[pname] = cobj
    return mdl


def __determineGroup(proxy):
    """Internal method"""
    if not proxy:
        return None
    xmlgroup = proxy.GetXMLGroup()
    xmlname = proxy.GetXMLName()
    if xmlgroup == "sources":
        return "sources"
    elif xmlgroup == "filters":
        return "sources"
    elif xmlgroup == "views":
        return "views"
    elif xmlgroup == "representations":
        if xmlname == "ScalarBarWidgetRepresentation":
            return "scalar_bars"
        return "representations"
    elif xmlgroup == "lookup_tables":
        return "lookup_tables"
    elif xmlgroup == "implicit_functions":
        return "implicit_functions"
    elif xmlgroup == "piecewise_functions":
        return "piecewise_functions"
    return None

__nameCounter = {}
def __determineName(proxy, group):
    global __nameCounter
    name = _make_name_valid(proxy.GetXMLLabel())
    if not name:
        return None
    if not __nameCounter.has_key(name):
        __nameCounter[name] = 1
        val = 1
    else:
        __nameCounter[name] += 1
        val = __nameCounter[name]
    return "%s%d" % (name, val)

def __getName(proxy, group):
    pxm = ProxyManager()
    if isinstance(proxy, Proxy):
        proxy = proxy.SMProxy
    return pxm.GetProxyName(group, proxy)

class MissingRegistrationInformation(Exception):
    """Exception for missing registration information. Raised when a name or group 
    is not specified or when a group cannot be deduced."""
    pass
    
def Register(proxy, **extraArgs):
    """Registers a proxy with the proxy manager. If no 'registrationGroup' is
    specified, then the group is inferred from the type of the proxy.
    'registrationName' may be specified to register with a particular name
    otherwise a default name will be created."""
    # TODO: handle duplicate registration
    if "registrationGroup" in extraArgs:
        registrationGroup = extraArgs["registrationGroup"]
    else:
        registrationGroup = __determineGroup(proxy)

    if "registrationName" in extraArgs:
        registrationName = extraArgs["registrationName"]
    else:
        registrationName = __determineName(proxy, registrationGroup)
    if registrationGroup and registrationName:
        pxm = ProxyManager()
        pxm.RegisterProxy(registrationGroup, registrationName, proxy)
    else:
        raise MissingRegistrationInformation, "Registration error %s %s." % (registrationGroup, registrationName)
    return (registrationGroup, registrationName)

def UnRegister(proxy, **extraArgs):
    """UnRegisters proxies registered using Register()."""
    if "registrationGroup" in extraArgs:
        registrationGroup = extraArgs["registrationGroup"]
    else:
        registrationGroup = __determineGroup(proxy)

    if "registrationName" in extraArgs:
        registrationName = extraArgs["registrationName"]
    else:
        registrationName = __getName(proxy, registrationGroup)

    if registrationGroup and registrationName:
        pxm = ProxyManager()
        pxm.UnRegisterProxy(registrationGroup, registrationName, proxy)
    else:
        raise RuntimeError, "UnRegistration error."
    return (registrationGroup, registrationName)

def demo1():
    """This simple demonstration creates a sphere, renders it and delivers
    it to the client using Fetch. It returns a tuple of (data, render
    view)"""
    if not ActiveConnection:
        Connect()
    if paraview.compatibility.GetVersion() <= 3.4:
        ss = sources.SphereSource(Radius=2, ThetaResolution=32)
        shr = filters.ShrinkFilter(Input=OutputPort(ss,0))
        cs = sources.ConeSource()
        app = filters.Append()
    else:
        ss = sources.Sphere(Radius=2, ThetaResolution=32)
        shr = filters.Shrink(Input=OutputPort(ss,0))
        cs = sources.Cone()
        app = filters.AppendDatasets()
    app.Input = [shr, cs]
    rv = CreateRenderView()
    rep = CreateRepresentation(app, rv)
    rv.ResetCamera()
    rv.StillRender()
    data = Fetch(ss)

    return (data, rv)

def demo2(fname="/Users/berk/Work/ParaViewData/Data/disk_out_ref.ex2"):
    """This method demonstrates the user of a reader, representation and
    view. It also demonstrates how meta-data can be obtained using proxies.
    Make sure to pass the full path to an exodus file. Also note that certain
    parameters are hard-coded for disk_out_ref.ex2 which can be found
    in ParaViewData. This method returns the render view."""
    if not ActiveConnection:
        Connect()
    # Create the exodus reader and specify a file name
    reader = sources.ExodusIIReader(FileName=fname)
    # Get the list of point arrays.
    if paraview.compatibility.GetVersion() <= 3.4:
        arraySelection = reader.PointResultArrayStatus
    else:
        arraySelection = reader.PointVariables
    print arraySelection.Available
    # Select all arrays
    arraySelection.SetData(arraySelection.Available)

    # Next create a default render view appropriate for the connection type.
    rv = CreateRenderView()
    # Create the matching representation
    rep = CreateRepresentation(reader, rv)
    rep.Representation = 1 # Wireframe
    # Black background is not pretty
    rv.Background = [0.4, 0.4, 0.6]
    rv.StillRender()
    # Reset the camera to include the whole thing
    rv.ResetCamera()
    rv.StillRender()
    # Change the elevation of the camera. See VTK documentation of vtkCamera
    # for camera parameters.
    c = rv.GetActiveCamera()
    c.Elevation(45)
    rv.StillRender()
    # Now that the reader execute, let's get some information about it's
    # output.
    pdi = reader[0].PointData
    # This prints a list of all read point data arrays as well as their
    # value ranges.
    print 'Number of point arrays:', len(pdi)
    for i in range(len(pdi)):
        ai = pdi[i]
        print "----------------"
        print "Array:", i, ai.Name, ":"
        numComps = ai.GetNumberOfComponents()
        print "Number of components:", numComps
        for j in range(numComps):
            if paraview.compatibility.GetVersion() <= 3.4:
                print "Range:", ai.Range(j)
            else:
                print "Range:", ai.GetRange(j)
    # White is boring. Let's color the geometry using a variable.
    # First create a lookup table. This object controls how scalar
    # values are mapped to colors. See VTK documentation for
    # details.
    lt = rendering.PVLookupTable()
    # Assign it to the representation
    rep.LookupTable = lt
    # Color by point array called Pres
    rep.ColorAttributeType = 0 # point data
    rep.ColorArrayName = "Pres"
    # Add to RGB points. These are tuples of 4 values. First one is
    # the scalar values, the other 3 the RGB values. This list has
    # 2 points: Pres: 0.00678, color: blue, Pres: 0.0288, color: red
    lt.RGBPoints = [0.00678, 0, 0, 1, 0.0288, 1, 0, 0]
    lt.ColorSpace = 1 # HSV
    rv.StillRender()
    return rv

def demo3():
    """This method demonstrates the use of servermanager with numpy as
    well as pylab for plotting. It creates an artificial data sources,
    probes it with a line, delivers the result to the client using Fetch
    and plots it using pylab. This demo requires numpy and pylab installed.
    It returns a tuple of (data, render view)."""
    import paraview.numpy_support
    import pylab

    if not ActiveConnection:
        Connect()
    # Create a synthetic data source
    if paraview.compatibility.GetVersion() <= 3.4:
        source = sources.RTAnalyticSource()
    else:
        source = sources.Wavelet()
    # Let's get some information about the data. First, for the
    # source to execute
    source.UpdatePipeline()

    di = source.GetDataInformation()
    print "Data type:", di.GetPrettyDataTypeString()
    print "Extent:", di.GetExtent()
    print "Array name:", \
          source[0].PointData[0].Name

    rv = CreateRenderView()

    rep1 = CreateRepresentation(source, rv)
    rep1.Representation = 3 # outline

    # Let's apply a contour filter
    cf = filters.Contour(Input=source, ContourValues=[200])

    # Select the array to contour by
    #cf.SelectInputScalars = 'RTData'

    rep2 = CreateRepresentation(cf, rv)

    rv.Background = (0.4, 0.4, 0.6)
    # Reset the camera to include the whole thing
    rv.StillRender()
    rv.ResetCamera()
    rv.StillRender()

    # Now, let's probe the data
    if paraview.compatibility.GetVersion() <= 3.4:
        probe = filters.Probe(Input=source)
        # with a line
        line = sources.LineSource(Resolution=60)
    else:
        probe = filters.ResampleWithDataset(Input=source)
        # with a line
        line = sources.Line(Resolution=60)
    # that spans the dataset
    bounds = di.GetBounds()
    print "Bounds: ", bounds
    line.Point1 = bounds[0:6:2]
    line.Point2 = bounds[1:6:2]

    probe.Source = line

    # Render with the line
    rep3 = CreateRepresentation(line, rv)
    rv.StillRender()

    # Now deliver it to the client. Remember, this is for small data.
    data = Fetch(probe)
    # Convert it to a numpy array
    data = paraview.numpy_support.vtk_to_numpy(
      data.GetPointData().GetArray("RTData"))
    # Plot it using matplotlib
    pylab.plot(data)
    pylab.show()

    return (data, rv, probe)

def demo4(fname="/Users/berk/Work/ParaViewData/Data/can.ex2"):
    """This method demonstrates the user of AnimateReader for
    creating animations."""
    if not ActiveConnection:
        Connect()
    reader = sources.ExodusIIReader(FileName=fname)
    view = CreateRenderView()
    repr = CreateRepresentation(reader, view)
    view.StillRender()
    view.ResetCamera()
    view.StillRender()
    c = view.GetActiveCamera()
    c.Elevation(95)
    return AnimateReader(reader, view)


def demo5():
    """ Simple sphere animation"""
    if not ActiveConnection:
        Connect()
    if paraview.compatibility.GetVersion() <= 3.4:
        sphere = sources.SphereSource()
    else:
        sphere = sources.Sphere()
    view = CreateRenderView()
    repr = CreateRepresentation(sphere, view)

    view.StillRender()
    view.ResetCamera()
    view.StillRender()

    # Create an animation scene
    scene = animation.AnimationScene()
    # Add 1 view
    scene.ViewModules = [view]

    # Create a cue to animate the StartTheta property
    cue = animation.KeyFrameAnimationCue()
    cue.AnimatedProxy = sphere
    cue.AnimatedPropertyName = "StartTheta"
    # Add it to the scene's cues
    scene.Cues = [cue]

    # Create 2 keyframes for the StartTheta track
    keyf0 = animation.CompositeKeyFrame()
    keyf0.Type = 2 # Set keyframe interpolation type to Ramp.
    # At time = 0, value = 0
    keyf0.KeyTime = 0
    keyf0.KeyValues= [0]

    keyf1 = animation.CompositeKeyFrame()
    # At time = 1.0, value = 200
    keyf1.KeyTime = 1.0
    keyf1.KeyValues= [200]

    # Add keyframes.
    cue.KeyFrames = [keyf0, keyf1]

    scene.Play()
    return scene

ASSOCIATIONS = { 'POINTS' : 0, 'CELLS' : 1, 'VERTICES' : 4, 'EDGES' : 5, 'ROWS' : 6}

# Users can set the active connection which will be used by API
# to create proxies etc when no connection argument is passed.
# Connect() automatically sets this if it is not already set.
ActiveConnection = None

# Needs to be called when paraview module is loaded from python instead
# of pvpython, pvbatch or GUI.
if not vtkSMObject.GetProxyManager():
    vtkInitializationHelper.Initialize(sys.executable)

# Initialize progress printing. Can be turned off by calling
# ToggleProgressPrinting() again.
progressObserverTag = None
currentAlgorithm = False
currentProgress = 0
fromGUI = False
ToggleProgressPrinting()

_pyproxies = {}

# Create needed sub-modules
_createModules()

# Set up our custom importer (if possible)
loader = _ModuleLoader()
sys.meta_path.append(loader)

if hasattr(sys, "ps1"):
    # session is interactive.
    print vtkSMProxyManager.GetParaViewSourceVersion();

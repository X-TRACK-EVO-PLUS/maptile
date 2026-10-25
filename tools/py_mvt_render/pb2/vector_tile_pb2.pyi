from google.protobuf.internal import containers as _containers
from google.protobuf.internal import enum_type_wrapper as _enum_type_wrapper
from google.protobuf.internal import python_message as _python_message
from google.protobuf import descriptor as _descriptor
from google.protobuf import message as _message
from typing import ClassVar as _ClassVar, Iterable as _Iterable, Mapping as _Mapping, Optional as _Optional, Union as _Union

DESCRIPTOR: _descriptor.FileDescriptor

class Tile(_message.Message):
    __slots__ = ("layers",)
    Extensions: _python_message._ExtensionDict
    class GeomType(int, metaclass=_enum_type_wrapper.EnumTypeWrapper):
        __slots__ = ()
        UNKNOWN: _ClassVar[Tile.GeomType]
        POINT: _ClassVar[Tile.GeomType]
        LINESTRING: _ClassVar[Tile.GeomType]
        POLYGON: _ClassVar[Tile.GeomType]
    UNKNOWN: Tile.GeomType
    POINT: Tile.GeomType
    LINESTRING: Tile.GeomType
    POLYGON: Tile.GeomType
    class Value(_message.Message):
        __slots__ = ("string_value", "float_value", "double_value", "int_value", "uint_value", "sint_value", "bool_value")
        Extensions: _python_message._ExtensionDict
        STRING_VALUE_FIELD_NUMBER: _ClassVar[int]
        FLOAT_VALUE_FIELD_NUMBER: _ClassVar[int]
        DOUBLE_VALUE_FIELD_NUMBER: _ClassVar[int]
        INT_VALUE_FIELD_NUMBER: _ClassVar[int]
        UINT_VALUE_FIELD_NUMBER: _ClassVar[int]
        SINT_VALUE_FIELD_NUMBER: _ClassVar[int]
        BOOL_VALUE_FIELD_NUMBER: _ClassVar[int]
        string_value: str
        float_value: float
        double_value: float
        int_value: int
        uint_value: int
        sint_value: int
        bool_value: bool
        def __init__(self, string_value: _Optional[str] = ..., float_value: _Optional[float] = ..., double_value: _Optional[float] = ..., int_value: _Optional[int] = ..., uint_value: _Optional[int] = ..., sint_value: _Optional[int] = ..., bool_value: bool = ...) -> None: ...
    class Feature(_message.Message):
        __slots__ = ("id", "tags", "type", "geometry")
        ID_FIELD_NUMBER: _ClassVar[int]
        TAGS_FIELD_NUMBER: _ClassVar[int]
        TYPE_FIELD_NUMBER: _ClassVar[int]
        GEOMETRY_FIELD_NUMBER: _ClassVar[int]
        id: int
        tags: _containers.RepeatedScalarFieldContainer[int]
        type: Tile.GeomType
        geometry: _containers.RepeatedScalarFieldContainer[int]
        def __init__(self, id: _Optional[int] = ..., tags: _Optional[_Iterable[int]] = ..., type: _Optional[_Union[Tile.GeomType, str]] = ..., geometry: _Optional[_Iterable[int]] = ...) -> None: ...
    class Layer(_message.Message):
        __slots__ = ("version", "name", "features", "keys", "values", "extent")
        Extensions: _python_message._ExtensionDict
        VERSION_FIELD_NUMBER: _ClassVar[int]
        NAME_FIELD_NUMBER: _ClassVar[int]
        FEATURES_FIELD_NUMBER: _ClassVar[int]
        KEYS_FIELD_NUMBER: _ClassVar[int]
        VALUES_FIELD_NUMBER: _ClassVar[int]
        EXTENT_FIELD_NUMBER: _ClassVar[int]
        version: int
        name: str
        features: _containers.RepeatedCompositeFieldContainer[Tile.Feature]
        keys: _containers.RepeatedScalarFieldContainer[str]
        values: _containers.RepeatedCompositeFieldContainer[Tile.Value]
        extent: int
        def __init__(self, version: _Optional[int] = ..., name: _Optional[str] = ..., features: _Optional[_Iterable[_Union[Tile.Feature, _Mapping]]] = ..., keys: _Optional[_Iterable[str]] = ..., values: _Optional[_Iterable[_Union[Tile.Value, _Mapping]]] = ..., extent: _Optional[int] = ...) -> None: ...
    LAYERS_FIELD_NUMBER: _ClassVar[int]
    layers: _containers.RepeatedCompositeFieldContainer[Tile.Layer]
    def __init__(self, layers: _Optional[_Iterable[_Union[Tile.Layer, _Mapping]]] = ...) -> None: ...

-- See README.txt for information and build instructions.

package "tutorial";

option {
   java_package = "com.example.tutorial";
   java_outer_classname = "AddressBookProtos";
}

message "Person" {
  required "name" :string(1);
  required "id" :int32(2);        -- Unique ID number for this person.
  optional "email" :string(3);

  enum "PhoneType" {
    MOBILE = 0;
    HOME = 1;
    WORK = 2;
  };

  message "PhoneNumber" {
    required "number" :string(1);
    optional "type" :PhoneType(2) {default = "HOME"};
  };

  repeated "phone" :PhoneNumber(4);
  repeated "test" :int32(5) {packed=true};

  extensions (10, "max");
}

message "Ext" {
  extend "Person" {
    optional "test" :int32(10);
  }
}

-- Our address book file is just one of these.
message "AddressBook" {
  repeated "person" :Person(1);
}


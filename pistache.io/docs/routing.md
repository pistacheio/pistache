---
title: Routing
---

HTTP routing consists of binding an HTTP route to a C++ callback. A special component called an HTTP router will be in charge of dispatching HTTP requests to the right C++ callback. A route is composed of an HTTP verb associated to a resource:

```javascript
GET /users/1
```

Here, `GET` is the verb and `/users/1` is the associated resource.

## HTTP methods

A bunch of HTTP methods (verbs) are supported by Pistache:

- _GET_: The `GET` method is used by the client (e.g browser) to retrieve a resource identified by an URI. For example, to retrieve an user identified by an id, a client will issue a `GET` to the `/users/:id` Request-URI.
- _POST_: the `POST` method is used to post or send new information to a certain resource. The server will then read and store the data associated to the request. `POST` is a common way of transmitting data from an HTML form. `POST` can also be used to create a new resource or update information of an existing resource. For example, to create a new user, a client will issue a `POST` to the `/users` path with the data of the user to create in its body.
- _PUT_: `PUT` is very similar to `POST` except that `PUT` is idempotent, meaning that two requests to the same Request-URI with the same identical content should have the same effect and should produce the same result.
- _DELETE_: the `DELETE` method is used to delete a resource associated to a given Request-URI. For example, to remove an user, a client might issue a `DELETE` call to the `/users/:id` Request-URI.

To sum up, `POST` and `PUT` are used to Create and/or Update, `GET` is used to Read and `DELETE` is used to Delete information.

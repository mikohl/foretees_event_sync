# foretees_event_sync
Custom Drupal 7 module that syncs content from a custom API.

The vendor ForeTees provides a service for the client to create event calendars
for their users and to track their registrations. This module pulls event data
as JSON from an API provided by ForeTees, and uses that data to create nodes
with the event details that are displayed in a calendar view.

The module was built for existing sites with an Event content type that were
previously entered manually. A previously developed ForeTees module is
required for Single Sign On functionality.
